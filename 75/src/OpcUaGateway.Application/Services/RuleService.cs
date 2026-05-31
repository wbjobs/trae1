using System.Text.Json;
using Dapper;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Application.Services;

public class RuleService : IRuleService
{
    private readonly ILogger<RuleService> _logger;
    private readonly GatewayConfig _config;
    private readonly string _connectionString;
    private readonly object _lock = new();
    private List<CalculationRule> _rulesCache = new();
    private DateTime _lastCacheRefresh = DateTime.MinValue;
    private readonly TimeSpan _cacheDuration = TimeSpan.FromSeconds(10);

    public RuleService(
        ILogger<RuleService> logger,
        IOptions<GatewayConfig> config)
    {
        _logger = logger;
        _config = config.Value;
        _connectionString = $"Data Source={_config.DatabasePath}";
    }

    public async Task InitializeAsync()
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                CREATE TABLE IF NOT EXISTS CalculationRules (
                    RuleId TEXT PRIMARY KEY,
                    RuleName TEXT NOT NULL,
                    DeviceId TEXT NOT NULL,
                    RuleType INTEGER NOT NULL,
                    SourcePointId TEXT NOT NULL,
                    OutputPointName TEXT NOT NULL,
                    OutputUnit TEXT,
                    Script TEXT NOT NULL,
                    Description TEXT,
                    IsEnabled INTEGER DEFAULT 1,
                    Status INTEGER DEFAULT 0,
                    TimeoutMs REAL DEFAULT 5.0,
                    WindowSize INTEGER DEFAULT 10,
                    MaxExecutionTimeMs REAL DEFAULT 5.0,
                    ExecutionFailCount INTEGER DEFAULT 0,
                    MaxFailCount INTEGER DEFAULT 5,
                    LastExecutionTime TEXT,
                    LastExecutionDurationMs REAL DEFAULT 0,
                    LastError TEXT,
                    CreatedAt TEXT NOT NULL,
                    UpdatedAt TEXT NOT NULL
                );
                CREATE INDEX IF NOT EXISTS IX_Rules_DeviceId ON CalculationRules(DeviceId);
                CREATE INDEX IF NOT EXISTS IX_Rules_IsEnabled ON CalculationRules(IsEnabled);
            ");

            _logger.LogInformation("Rule service initialized");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to initialize rule service");
            throw;
        }
    }

    public async Task<List<CalculationRule>> GetRulesAsync(string? deviceId = null)
    {
        lock (_lock)
        {
            if ((DateTime.UtcNow - _lastCacheRefresh) < _cacheDuration && deviceId == null)
            {
                return new List<CalculationRule>(_rulesCache);
            }
        }

        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var sql = "SELECT * FROM CalculationRules";
        if (!string.IsNullOrEmpty(deviceId))
        {
            sql += " WHERE DeviceId = @DeviceId";
        }
        sql += " ORDER BY CreatedAt DESC";

        var rules = (await connection.QueryAsync<CalculationRule>(sql,
            new { DeviceId = deviceId })).ToList();

        if (deviceId == null)
        {
            lock (_lock)
            {
                _rulesCache = new List<CalculationRule>(rules);
                _lastCacheRefresh = DateTime.UtcNow;
            }
        }

        return rules;
    }

    public async Task<CalculationRule?> GetRuleAsync(string ruleId)
    {
        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        return await connection.QueryFirstOrDefaultAsync<CalculationRule>(
            "SELECT * FROM CalculationRules WHERE RuleId = @RuleId",
            new { RuleId = ruleId });
    }

    public async Task<CalculationRule> AddRuleAsync(CalculationRule rule)
    {
        rule.RuleId = string.IsNullOrEmpty(rule.RuleId) ? Guid.NewGuid().ToString() : rule.RuleId;
        rule.CreatedAt = DateTime.UtcNow;
        rule.UpdatedAt = DateTime.UtcNow;
        rule.Status = RuleExecutionStatus.Active;
        rule.ExecutionFailCount = 0;

        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        await connection.ExecuteAsync(@"
            INSERT INTO CalculationRules (
                RuleId, RuleName, DeviceId, RuleType, SourcePointId,
                OutputPointName, OutputUnit, Script, Description,
                IsEnabled, Status, TimeoutMs, WindowSize, MaxExecutionTimeMs,
                ExecutionFailCount, MaxFailCount, LastExecutionTime,
                LastExecutionDurationMs, LastError, CreatedAt, UpdatedAt
            ) VALUES (
                @RuleId, @RuleName, @DeviceId, @RuleType, @SourcePointId,
                @OutputPointName, @OutputUnit, @Script, @Description,
                @IsEnabled, @Status, @TimeoutMs, @WindowSize, @MaxExecutionTimeMs,
                @ExecutionFailCount, @MaxFailCount, @LastExecutionTime,
                @LastExecutionDurationMs, @LastError, @CreatedAt, @UpdatedAt
            )", rule);

        InvalidateCache();
        _logger.LogInformation("Rule added: {RuleName} (Id: {RuleId})", rule.RuleName, rule.RuleId);
        return rule;
    }

    public async Task<CalculationRule?> UpdateRuleAsync(string ruleId, CalculationRule rule)
    {
        var existing = await GetRuleAsync(ruleId);
        if (existing == null) return null;

        rule.RuleId = ruleId;
        rule.UpdatedAt = DateTime.UtcNow;
        rule.CreatedAt = existing.CreatedAt;

        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        await connection.ExecuteAsync(@"
            UPDATE CalculationRules SET
                RuleName = @RuleName,
                DeviceId = @DeviceId,
                RuleType = @RuleType,
                SourcePointId = @SourcePointId,
                OutputPointName = @OutputPointName,
                OutputUnit = @OutputUnit,
                Script = @Script,
                Description = @Description,
                IsEnabled = @IsEnabled,
                Status = @Status,
                TimeoutMs = @TimeoutMs,
                WindowSize = @WindowSize,
                MaxExecutionTimeMs = @MaxExecutionTimeMs,
                MaxFailCount = @MaxFailCount,
                UpdatedAt = @UpdatedAt
            WHERE RuleId = @RuleId", rule);

        InvalidateCache();
        _logger.LogInformation("Rule updated: {RuleName} (Id: {RuleId})", rule.RuleName, ruleId);
        return rule;
    }

    public async Task<bool> DeleteRuleAsync(string ruleId)
    {
        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var rows = await connection.ExecuteAsync(
            "DELETE FROM CalculationRules WHERE RuleId = @RuleId",
            new { RuleId = ruleId });

        InvalidateCache();
        _logger.LogInformation("Rule deleted: {RuleId}", ruleId);
        return rows > 0;
    }

    public async Task<bool> SetEnabledAsync(string ruleId, bool enabled)
    {
        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        var rows = await connection.ExecuteAsync(@"
            UPDATE CalculationRules SET
                IsEnabled = @IsEnabled,
                Status = @Status,
                UpdatedAt = @UpdatedAt
            WHERE RuleId = @RuleId", new
        {
            IsEnabled = enabled ? 1 : 0,
            Status = enabled ? (int)RuleExecutionStatus.Active : (int)RuleExecutionStatus.Disabled,
            UpdatedAt = DateTime.UtcNow,
            RuleId = ruleId
        });

        InvalidateCache();
        _logger.LogInformation("Rule {RuleId} enabled set to {Enabled}", ruleId, enabled);
        return rows > 0;
    }

    public async Task UpdateRuleExecutionStateAsync(CalculationRule rule)
    {
        using var connection = new SqliteConnection(_connectionString);
        await connection.OpenAsync();

        await connection.ExecuteAsync(@"
            UPDATE CalculationRules SET
                Status = @Status,
                ExecutionFailCount = @ExecutionFailCount,
                LastExecutionTime = @LastExecutionTime,
                LastExecutionDurationMs = @LastExecutionDurationMs,
                LastError = @LastError,
                UpdatedAt = @UpdatedAt
            WHERE RuleId = @RuleId", new
        {
            rule.Status,
            rule.ExecutionFailCount,
            rule.LastExecutionTime,
            rule.LastExecutionDurationMs,
            rule.LastError,
            UpdatedAt = DateTime.UtcNow,
            rule.RuleId
        });
    }

    public Task<List<VirtualDataPoint>> GetVirtualPointsAsync(string deviceId)
    {
        var points = new List<VirtualDataPoint>();
        return Task.FromResult(points);
    }

    public Task SaveChangesAsync()
    {
        InvalidateCache();
        return Task.CompletedTask;
    }

    private void InvalidateCache()
    {
        lock (_lock)
        {
            _lastCacheRefresh = DateTime.MinValue;
        }
    }
}