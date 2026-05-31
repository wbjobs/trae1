namespace OpcUaGateway.Core.Models;

public enum RuleType
{
    UnitConversion,
    MovingAverage,
    RateCalculation,
    CustomScript,
    Expression
}

public enum RuleExecutionStatus
{
    Active,
    Disabled,
    Error,
    AutoDisabled_Performance,
    AutoDisabled_Error
}

public class CalculationRule
{
    public string RuleId { get; set; } = Guid.NewGuid().ToString();
    public string RuleName { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public RuleType RuleType { get; set; } = RuleType.CustomScript;
    public string SourcePointId { get; set; } = string.Empty;
    public string OutputPointName { get; set; } = string.Empty;
    public string OutputUnit { get; set; } = string.Empty;
    public string Script { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public bool IsEnabled { get; set; } = true;
    public RuleExecutionStatus Status { get; set; } = RuleExecutionStatus.Active;
    public double TimeoutMs { get; set; } = 5.0;
    public int WindowSize { get; set; } = 10;
    public double MaxExecutionTimeMs { get; set; } = 5.0;
    public int ExecutionFailCount { get; set; }
    public int MaxFailCount { get; set; } = 5;
    public DateTime LastExecutionTime { get; set; }
    public double LastExecutionDurationMs { get; set; }
    public string? LastError { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime UpdatedAt { get; set; } = DateTime.UtcNow;
}

public class VirtualDataPoint
{
    public string PointId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string RuleId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public string Unit { get; set; } = string.Empty;
    public double Value { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
    public int Quality { get; set; } = 192;
    public double UpperThreshold { get; set; }
    public double LowerThreshold { get; set; }
}

public class RuleExecutionResult
{
    public bool Success { get; set; }
    public double Value { get; set; }
    public double ExecutionDurationMs { get; set; }
    public string? ErrorMessage { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public class RulePerformanceStats
{
    public string RuleId { get; set; } = string.Empty;
    public string RuleName { get; set; } = string.Empty;
    public int TotalExecutions { get; set; }
    public int SuccessCount { get; set; }
    public int FailureCount { get; set; }
    public double AverageExecutionMs { get; set; }
    public double MaxExecutionMs { get; set; }
    public double MinExecutionMs { get; set; }
    public double P95ExecutionMs { get; set; }
    public double P99ExecutionMs { get; set; }
    public int AutoDisabledCount { get; set; }
    public DateTime LastExecution { get; set; }
    public double LastExecutionMs { get; set; }
    public RuleExecutionStatus CurrentStatus { get; set; } = RuleExecutionStatus.Active;
}

public class RuleDebugRequest
{
    public string RuleName { get; set; } = string.Empty;
    public string Script { get; set; } = string.Empty;
    public Dictionary<string, double> Inputs { get; set; } = new();
    public string? SourcePointId { get; set; }
    public double? TestValue { get; set; }
}

public class RuleDebugResponse
{
    public bool Success { get; set; }
    public double Result { get; set; }
    public double ExecutionMs { get; set; }
    public string? ErrorMessage { get; set; }
    public Dictionary<string, double> Variables { get; set; } = new();
}