using System.Collections.Concurrent;
using System.Diagnostics;
using System.Text.Json;
using Jint;
using Jint.Runtime;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Logging;

namespace OpcUaGateway.Application.Services;

public class RuleEngineService : IRuleEngine
{
    private readonly ILogger<RuleEngineService> _logger;
    private readonly IOptions<GatewayConfig> _config;
    private readonly ConcurrentDictionary<string, Engine> _engines = new();
    private readonly ConcurrentDictionary<string, RulePerformanceStats> _performanceStats = new();
    private readonly ConcurrentDictionary<string, ConcurrentQueue<double>> _executionTimes = new();
    private readonly ConcurrentDictionary<string, List<double>> _movingAverageWindows = new();
    private readonly ConcurrentDictionary<string, double> _previousValues = new();
    private readonly ConcurrentDictionary<string, DateTime> _previousTimestamps = new();
    private readonly object _lock = new();

    public RuleEngineService(ILogger<RuleEngineService> logger, IOptions<GatewayConfig> config)
    {
        _logger = logger;
        _config = config;
    }

    public async Task InitializeAsync()
    {
        _logger.LogInformation("Initializing Rule Engine...");
        await Task.CompletedTask;
    }

    public async Task<RuleExecutionResult> ExecuteRuleAsync(
        CalculationRule rule,
        double inputValue,
        Dictionary<string, double>? context = null)
    {
        if (!rule.IsEnabled || rule.Status == RuleExecutionStatus.AutoDisabled_Performance ||
            rule.Status == RuleExecutionStatus.AutoDisabled_Error)
        {
            return new RuleExecutionResult
            {
                Success = false,
                ErrorMessage = $"Rule '{rule.RuleName}' is disabled (status: {rule.Status})"
            };
        }

        var sw = Stopwatch.StartNew();

        try
        {
            double result;

            switch (rule.RuleType)
            {
                case RuleType.UnitConversion:
                case RuleType.Expression:
                case RuleType.CustomScript:
                    result = await ExecuteScriptAsync(rule, inputValue, context);
                    break;

                case RuleType.MovingAverage:
                    result = CalculateMovingAverage(rule, inputValue);
                    break;

                case RuleType.RateCalculation:
                    result = CalculateRate(rule, inputValue);
                    break;

                default:
                    result = await ExecuteScriptAsync(rule, inputValue, context);
                    break;
            }

            sw.Stop();
            var durationMs = sw.Elapsed.TotalMilliseconds;

            RecordPerformance(rule.RuleId, durationMs, true);
            CheckPerformanceThreshold(rule, durationMs);

            return new RuleExecutionResult
            {
                Success = true,
                Value = result,
                ExecutionDurationMs = durationMs,
                Timestamp = DateTime.UtcNow
            };
        }
        catch (JavaScriptException ex)
        {
            sw.Stop();
            var durationMs = sw.Elapsed.TotalMilliseconds;
            _logger.LogError(ex, "JavaScript execution error in rule {RuleName}", rule.RuleName);

            HandleExecutionFailure(rule, ex.Message);
            RecordPerformance(rule.RuleId, durationMs, false);

            return new RuleExecutionResult
            {
                Success = false,
                Value = 0,
                ExecutionDurationMs = durationMs,
                ErrorMessage = $"Script error: {ex.Message}"
            };
        }
        catch (Exception ex)
        {
            sw.Stop();
            var durationMs = sw.Elapsed.TotalMilliseconds;
            _logger.LogError(ex, "Unexpected error executing rule {RuleName}", rule.RuleName);

            HandleExecutionFailure(rule, ex.Message);
            RecordPerformance(rule.RuleId, durationMs, false);

            return new RuleExecutionResult
            {
                Success = false,
                Value = 0,
                ExecutionDurationMs = durationMs,
                ErrorMessage = ex.Message
            };
        }
    }

    private Task<double> ExecuteScriptAsync(
        CalculationRule rule,
        double inputValue,
        Dictionary<string, double>? context)
    {
        var engine = GetOrCreateEngine(rule);

        lock (_lock)
        {
            engine.SetValue("value", inputValue);
            engine.SetValue("input", inputValue);

            if (context != null)
            {
                foreach (var kvp in context)
                {
                    engine.SetValue(kvp.Key, kvp.Value);
                }
            }

            var script = rule.Script;
            if (rule.RuleType == RuleType.Expression)
            {
                script = $"({script})";
            }

            var result = engine.Evaluate(script);
            var numericResult = result.AsNumber();

            return Task.FromResult(numericResult);
        }
    }

    private Engine GetOrCreateEngine(CalculationRule rule)
    {
        if (_engines.TryGetValue(rule.RuleId, out var existing))
        {
            return existing;
        }

        lock (_lock)
        {
            if (_engines.TryGetValue(rule.RuleId, out var engine))
            {
                return engine;
            }

            var newEngine = new Engine(options =>
            {
                options.TimeoutInterval(TimeSpan.FromMilliseconds(rule.MaxExecutionTimeMs));
                options.LimitRecursion(10);
                options.Strict = true;
            });

            newEngine.SetValue("log", new Action<object>(obj =>
                _logger.LogDebug("Rule {RuleName} log: {Message}", rule.RuleName, obj)));
            newEngine.SetValue("round", new Func<double, int, double>(Math.Round));
            newEngine.SetValue("abs", new Func<double, double>(Math.Abs));
            newEngine.SetValue("min", new Func<double, double, double>(Math.Min));
            newEngine.SetValue("max", new Func<double, double, double>(Math.Max));
            newEngine.SetValue("clamp", new Func<double, double, double, double>((val, min, max) =>
                Math.Min(Math.Max(val, min), max)));

            _engines[rule.RuleId] = newEngine;
            return newEngine;
        }
    }

    private double CalculateMovingAverage(CalculationRule rule, double inputValue)
    {
        var window = _movingAverageWindows.GetOrAdd(rule.RuleId, _ => new List<double>());

        lock (window)
        {
            window.Add(inputValue);
            if (window.Count > rule.WindowSize)
            {
                window.RemoveAt(0);
            }

            if (window.Count == 0) return inputValue;
            return window.Average();
        }
    }

    private double CalculateRate(CalculationRule rule, double inputValue)
    {
        var now = DateTime.UtcNow;
        var prevVal = _previousValues.GetOrAdd(rule.RuleId, _ => inputValue);
        var prevTime = _previousTimestamps.GetOrAdd(rule.RuleId, _ => now);

        _previousValues[rule.RuleId] = inputValue;
        _previousTimestamps[rule.RuleId] = now;

        var elapsed = (now - prevTime).TotalSeconds;
        if (elapsed <= 0) return 0;

        return (inputValue - prevVal) / elapsed;
    }

    private void RecordPerformance(string ruleId, double durationMs, bool success)
    {
        var stats = _performanceStats.GetOrAdd(ruleId, _ => new RulePerformanceStats
        {
            RuleId = ruleId,
            RuleName = ruleId
        });

        stats.TotalExecutions++;
        if (success) stats.SuccessCount++;
        else stats.FailureCount++;

        if (stats.MaxExecutionMs == 0 || durationMs > stats.MaxExecutionMs)
            stats.MaxExecutionMs = durationMs;
        if (stats.MinExecutionMs == 0 || durationMs < stats.MinExecutionMs)
            stats.MinExecutionMs = durationMs;

        stats.AverageExecutionMs = stats.AverageExecutionMs == 0
            ? durationMs
            : stats.AverageExecutionMs * 0.95 + durationMs * 0.05;

        stats.LastExecution = DateTime.UtcNow;
        stats.LastExecutionMs = durationMs;

        var times = _executionTimes.GetOrAdd(ruleId, _ => new ConcurrentQueue<double>());
        times.Enqueue(durationMs);
        while (times.Count > _config.Value.RuleEngine.PerformanceSampleSize && times.TryDequeue(out _)) { }

        CalculatePercentiles(ruleId, stats);
    }

    private void CalculatePercentiles(string ruleId, RulePerformanceStats stats)
    {
        if (_executionTimes.TryGetValue(ruleId, out var times) && times.Count >= 10)
        {
            var sorted = times.OrderBy(x => x).ToList();
            var p95Index = (int)Math.Ceiling(sorted.Count * 0.95) - 1;
            var p99Index = (int)Math.Ceiling(sorted.Count * 0.99) - 1;
            stats.P95ExecutionMs = sorted[Math.Max(0, p95Index)];
            stats.P99ExecutionMs = sorted[Math.Max(0, p99Index)];
        }
    }

    private void CheckPerformanceThreshold(CalculationRule rule, double durationMs)
    {
        if (durationMs > rule.MaxExecutionTimeMs)
        {
            _logger.LogWarning(
                "Rule {RuleName} exceeded execution time limit: {Duration}ms (max: {Max}ms)",
                rule.RuleName, Math.Round(durationMs, 2), rule.MaxExecutionTimeMs);
        }
    }

    private void HandleExecutionFailure(CalculationRule rule, string error)
    {
        rule.ExecutionFailCount++;
        rule.LastError = error;
        rule.LastExecutionTime = DateTime.UtcNow;

        if (rule.ExecutionFailCount >= rule.MaxFailCount)
        {
            rule.Status = RuleExecutionStatus.AutoDisabled_Error;
            _logger.LogCritical(
                "Rule {RuleName} auto-disabled due to {Count} consecutive failures. Last error: {Error}",
                rule.RuleName, rule.ExecutionFailCount, error);

            if (_performanceStats.TryGetValue(rule.RuleId, out var stats))
            {
                stats.AutoDisabledCount++;
                stats.CurrentStatus = RuleExecutionStatus.AutoDisabled_Error;
            }

            RuleAutoDisabled?.Invoke(this, new RuleDisabledEventArgs
            {
                RuleId = rule.RuleId,
                RuleName = rule.RuleName,
                Reason = $"Auto-disabled after {rule.ExecutionFailCount} consecutive errors: {error}",
                Timestamp = DateTime.UtcNow
            });
        }
    }

    public Task<RuleDebugResponse> DebugRuleAsync(RuleDebugRequest request)
    {
        var sw = Stopwatch.StartNew();

        try
        {
            var engine = new Engine(options =>
            {
                options.TimeoutInterval(TimeSpan.FromSeconds(10));
                options.LimitRecursion(20);
                options.Strict = true;
            });

            var variables = new Dictionary<string, double>();

            if (request.TestValue.HasValue)
            {
                engine.SetValue("value", request.TestValue.Value);
                engine.SetValue("input", request.TestValue.Value);
                variables["value"] = request.TestValue.Value;
                variables["input"] = request.TestValue.Value;
            }

            foreach (var kvp in request.Inputs)
            {
                engine.SetValue(kvp.Key, kvp.Value);
                variables[kvp.Key] = kvp.Value;
            }

            engine.SetValue("log", new Action<object>(obj => { }));
            engine.SetValue("round", new Func<double, int, double>(Math.Round));
            engine.SetValue("abs", new Func<double, double>(Math.Abs));
            engine.SetValue("min", new Func<double, double, double>(Math.Min));
            engine.SetValue("max", new Func<double, double, double>(Math.Max));
            engine.SetValue("clamp", new Func<double, double, double, double>((val, min, max) =>
                Math.Min(Math.Max(val, min), max)));

            var script = request.Script;
            if (request.Script.Trim().StartsWith("function"))
            {
                script = request.Script;
            }
            else if (!request.Script.Trim().StartsWith("return"))
            {
                script = $"return ({request.Script});";
            }

            var wrapper = $"(function() {{ {script} }})()";
            var result = engine.Evaluate(wrapper);
            var numericResult = result.AsNumber();

            sw.Stop();

            return Task.FromResult(new RuleDebugResponse
            {
                Success = true,
                Result = numericResult,
                ExecutionMs = sw.Elapsed.TotalMilliseconds,
                Variables = variables
            });
        }
        catch (Exception ex)
        {
            sw.Stop();
            _logger.LogError(ex, "Debug rule execution failed");

            return Task.FromResult(new RuleDebugResponse
            {
                Success = false,
                ExecutionMs = sw.Elapsed.TotalMilliseconds,
                ErrorMessage = ex.Message
            });
        }
    }

    public Task<RulePerformanceStats> GetPerformanceStatsAsync(string ruleId)
    {
        var stats = _performanceStats.GetOrAdd(ruleId, _ => new RulePerformanceStats
        {
            RuleId = ruleId,
            RuleName = ruleId
        });
        return Task.FromResult(stats);
    }

    public Task<List<RulePerformanceStats>> GetAllPerformanceStatsAsync()
    {
        return Task.FromResult(_performanceStats.Values.ToList());
    }

    public Task<int> GetExecutionCountAsync(string ruleId)
    {
        var count = _performanceStats.TryGetValue(ruleId, out var stats) ? stats.TotalExecutions : 0;
        return Task.FromResult(count);
    }

    public Task SetEnabledAsync(string ruleId, bool enabled)
    {
        if (_engines.TryRemove(ruleId, out var engine))
        {
            try { engine.Dispose(); } catch { }
        }

        if (_performanceStats.TryGetValue(ruleId, out var stats))
        {
            stats.CurrentStatus = enabled ? RuleExecutionStatus.Active : RuleExecutionStatus.Disabled;
        }

        _movingAverageWindows.TryRemove(ruleId, out _);
        _previousValues.TryRemove(ruleId, out _);
        _previousTimestamps.TryRemove(ruleId, out _);

        _logger.LogInformation("Rule engine {RuleId} reloaded (enabled: {Enabled})", ruleId, enabled);
        return Task.CompletedTask;
    }

    public event EventHandler<RuleExecutionEventArgs>? RuleExecuted;
    public event EventHandler<RuleDisabledEventArgs>? RuleAutoDisabled;

    public async ValueTask DisposeAsync()
    {
        foreach (var engine in _engines.Values)
        {
            try { engine.Dispose(); } catch { }
        }
        _engines.Clear();
        await Task.CompletedTask;
    }
}