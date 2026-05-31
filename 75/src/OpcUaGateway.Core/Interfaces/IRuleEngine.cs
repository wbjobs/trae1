using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface IRuleEngine : IAsyncDisposable
{
    Task InitializeAsync();

    Task<RuleExecutionResult> ExecuteRuleAsync(CalculationRule rule, double inputValue, Dictionary<string, double>? context = null);

    Task<RuleDebugResponse> DebugRuleAsync(RuleDebugRequest request);

    Task<RulePerformanceStats> GetPerformanceStatsAsync(string ruleId);

    Task<List<RulePerformanceStats>> GetAllPerformanceStatsAsync();

    Task<int> GetExecutionCountAsync(string ruleId);

    Task SetEnabledAsync(string ruleId, bool enabled);

    event EventHandler<RuleExecutionEventArgs>? RuleExecuted;
    event EventHandler<RuleDisabledEventArgs>? RuleAutoDisabled;
}

public class RuleExecutionEventArgs : EventArgs
{
    public string RuleId { get; set; } = string.Empty;
    public string RuleName { get; set; } = string.Empty;
    public double InputValue { get; set; }
    public double OutputValue { get; set; }
    public double ExecutionMs { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public class RuleDisabledEventArgs : EventArgs
{
    public string RuleId { get; set; } = string.Empty;
    public string RuleName { get; set; } = string.Empty;
    public string Reason { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public interface IRuleService
{
    Task InitializeAsync();

    Task<List<CalculationRule>> GetRulesAsync(string? deviceId = null);

    Task<CalculationRule?> GetRuleAsync(string ruleId);

    Task<CalculationRule> AddRuleAsync(CalculationRule rule);

    Task<CalculationRule?> UpdateRuleAsync(string ruleId, CalculationRule rule);

    Task<bool> DeleteRuleAsync(string ruleId);

    Task<bool> SetEnabledAsync(string ruleId, bool enabled);

    Task<List<VirtualDataPoint>> GetVirtualPointsAsync(string deviceId);

    Task SaveChangesAsync();
}