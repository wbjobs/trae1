using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Configuration;

public class AppSettings
{
    public GatewayConfig Gateway { get; set; } = new();
}

public class GatewayConfig
{
    public int PollIntervalMs { get; set; } = 100;
    public int BatchSize { get; set; } = 50;
    public int OfflineFlushIntervalMs { get; set; } = 5000;
    public int MaxRetryCount { get; set; } = 3;
    public string DatabasePath { get; set; } = "gateway_data.db";
    public MqttConfig Mqtt { get; set; } = new();
    public CertificateConfig Certificate { get; set; } = new();
    public EmailConfig Email { get; set; } = new();
    public RuleEngineConfig RuleEngine { get; set; } = new();
}

public class MqttConfig
{
    public string BrokerAddress { get; set; } = "localhost";
    public int Port { get; set; } = 1883;
    public string ClientId { get; set; } = "OpcUaGateway";
    public string? Username { get; set; }
    public string? Password { get; set; }
    public string TopicPrefix { get; set; } = "factory/gateway";
    public bool UseTls { get; set; }
}

public class CertificateConfig
{
    public CertificateAcceptanceStrategy AcceptanceStrategy { get; set; } = CertificateAcceptanceStrategy.AutoTrustAll;
    public int ExpiryWarningDays { get; set; } = 30;
    public bool AutoRenegotiateOnChange { get; set; } = true;
    public int AuditLogRetentionDays { get; set; } = 90;
}

public class EmailConfig
{
    public bool EnableNotifications { get; set; } = true;
    public string SmtpServer { get; set; } = string.Empty;
    public int SmtpPort { get; set; } = 587;
    public string? Username { get; set; }
    public string? Password { get; set; }
    public bool UseSsl { get; set; } = true;
    public string FromAddress { get; set; } = "noreply@gateway.local";
    public string FromName { get; set; } = "OPC UA Gateway";
    public List<string> AdminEmails { get; set; } = new();
}

public class RuleEngineConfig
{
    public bool EnableRuleEngine { get; set; } = true;
    public double MaxExecutionTimeMs { get; set; } = 5.0;
    public int MaxFailCount { get; set; } = 5;
    public int PerformanceSampleSize { get; set; } = 1000;
    public int AutoDisablePerformanceThresholdMs { get; set; } = 5;
    public bool EnablePerformanceMonitoring { get; set; } = true;
    public string ScriptBasePath { get; set; } = "rules";
}