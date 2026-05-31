namespace OpcUaGateway.Core.DTOs;

public class DeviceConfigDto
{
    public string DeviceName { get; set; } = string.Empty;
    public string EndpointUrl { get; set; } = string.Empty;
    public string? Username { get; set; }
    public string? Password { get; set; }
    public bool UseSecurity { get; set; }
    public bool IsEnabled { get; set; } = true;
    public int PollIntervalMs { get; set; } = 100;
}

public class DataPointDto
{
    public string NodeId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public string DataType { get; set; } = "Double";
    public string Unit { get; set; } = string.Empty;
    public string? Description { get; set; }
    public bool IsEnabled { get; set; } = true;
    public double? UpperThreshold { get; set; }
    public double? LowerThreshold { get; set; }
    public double? RateOfChangeThreshold { get; set; }
}

public class ThresholdConfigDto
{
    public string DeviceId { get; set; } = string.Empty;
    public string PointId { get; set; } = string.Empty;
    public double? UpperThreshold { get; set; }
    public double? LowerThreshold { get; set; }
    public double? RateOfChangeThreshold { get; set; }
}

public class DeviceStatusDto
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public string Status { get; set; } = string.Empty;
    public DateTime LastUpdateTime { get; set; }
    public DateTime LastSuccessfulCollectTime { get; set; }
    public bool IsConnected { get; set; }
    public string? ErrorMessage { get; set; }
    public int SuccessfulCollectCount { get; set; }
    public int FailedCollectCount { get; set; }
}

public class AlertDto
{
    public string AlertId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public string PointId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public string AlertType { get; set; } = string.Empty;
    public double ThresholdValue { get; set; }
    public double ActualValue { get; set; }
    public DateTime TriggerTime { get; set; }
    public bool IsAcknowledged { get; set; }
    public string? Message { get; set; }
}

public class DataPointValueDto
{
    public string PointId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string Unit { get; set; } = string.Empty;
    public DateTime LastUpdateTime { get; set; }
    public int Quality { get; set; }
}

public class GatewayConfigDto
{
    public MqttBrokerConfig Mqtt { get; set; } = new();
    public int BatchSize { get; set; } = 50;
    public int OfflineFlushIntervalMs { get; set; } = 5000;
    public int MaxRetryCount { get; set; } = 3;
    public string DatabasePath { get; set; } = "gateway_data.db";
}

public class MqttBrokerConfig
{
    public string BrokerAddress { get; set; } = "localhost";
    public int Port { get; set; } = 1883;
    public string ClientId { get; set; } = "OpcUaGateway";
    public string? Username { get; set; }
    public string? Password { get; set; }
    public string TopicPrefix { get; set; } = "factory/gateway";
    public bool UseTls { get; set; }
}

public class CertificateConfigDto
{
    public string AcceptanceStrategy { get; set; } = "AutoTrustAll";
    public int ExpiryWarningDays { get; set; } = 30;
    public bool AutoRenegotiateOnChange { get; set; } = true;
    public int AuditLogRetentionDays { get; set; } = 90;
}

public class CertificateWhitelistDto
{
    public long Id { get; set; }
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string Issuer { get; set; } = string.Empty;
    public DateTime NotAfter { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string? Description { get; set; }
    public DateTime AddedAt { get; set; }
    public string? AddedBy { get; set; }
    public bool IsEnabled { get; set; } = true;
}

public class CertificateAuditLogDto
{
    public long Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public string EventType { get; set; } = string.Empty;
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string? PreviousThumbprint { get; set; }
    public DateTime EventTime { get; set; }
    public string? Details { get; set; }
    public string StrategyUsed { get; set; } = string.Empty;
    public bool IsAccepted { get; set; }
    public string? Operator { get; set; }
}

public class CertificateInfoDto
{
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string Issuer { get; set; } = string.Empty;
    public DateTime NotBefore { get; set; }
    public DateTime NotAfter { get; set; }
    public string SerialNumber { get; set; } = string.Empty;
    public string SignatureAlgorithm { get; set; } = string.Empty;
    public bool IsSelfSigned { get; set; }
    public bool IsExpired { get; set; }
    public int DaysUntilExpiry { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
}

public class WhitelistAddDto
{
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string Issuer { get; set; } = string.Empty;
    public DateTime NotAfter { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string? Description { get; set; }
}

public class DeviceDto
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public string EndpointUrl { get; set; } = string.Empty;
    public bool IsEnabled { get; set; } = true;
}

public class RuleDto
{
    public string RuleId { get; set; } = string.Empty;
    public string RuleName { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string RuleType { get; set; } = string.Empty;
    public string SourcePointId { get; set; } = string.Empty;
    public string OutputPointName { get; set; } = string.Empty;
    public string OutputUnit { get; set; } = string.Empty;
    public string Script { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public bool IsEnabled { get; set; } = true;
    public string Status { get; set; } = string.Empty;
    public int WindowSize { get; set; } = 10;
    public double MaxExecutionTimeMs { get; set; } = 5.0;
    public int MaxFailCount { get; set; } = 5;
}

public class RulePerformanceDto
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
    public string CurrentStatus { get; set; } = string.Empty;
}

public class RuleDebugDto
{
    public string RuleName { get; set; } = string.Empty;
    public string Script { get; set; } = string.Empty;
    public string? SourcePointId { get; set; }
    public double? TestValue { get; set; }
    public Dictionary<string, double> Inputs { get; set; } = new();
}

public class RuleDebugResponseDto
{
    public bool Success { get; set; }
    public double Result { get; set; }
    public double ExecutionMs { get; set; }
    public string? ErrorMessage { get; set; }
    public Dictionary<string, double> Variables { get; set; } = new();
}

public class VirtualPointDto
{
    public string PointId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string RuleId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public string Unit { get; set; } = string.Empty;
    public double Value { get; set; }
    public DateTime Timestamp { get; set; }
    public int Quality { get; set; }
}