namespace OpcUaGateway.Core.Models;

public enum CertificateAcceptanceStrategy
{
    AutoTrustAll,
    WhitelistOnly,
    ManualConfirmation
}

public enum CertificateEventType
{
    CertificateDetected,
    CertificateChanged,
    CertificateExpired,
    CertificateRenewed,
    CertificateRejected,
    CertificateAccepted,
    CertificateValidationFailed
}

public class CertificateInfo
{
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string Issuer { get; set; } = string.Empty;
    public DateTime NotBefore { get; set; }
    public DateTime NotAfter { get; set; }
    public string SerialNumber { get; set; } = string.Empty;
    public string PublicKey { get; set; } = string.Empty;
    public string SignatureAlgorithm { get; set; } = string.Empty;
    public bool IsSelfSigned { get; set; }

    public bool IsExpired => DateTime.UtcNow > NotAfter;
    public int DaysUntilExpiry => (NotAfter - DateTime.UtcNow).Days;

    public override bool Equals(object? obj)
    {
        if (obj is CertificateInfo other)
            return Thumbprint.Equals(other.Thumbprint, StringComparison.OrdinalIgnoreCase);
        return false;
    }

    public override int GetHashCode() => Thumbprint.GetHashCode();
}

public class CertificateWhitelistEntry
{
    public long Id { get; set; }
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string Issuer { get; set; } = string.Empty;
    public DateTime NotAfter { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string? Description { get; set; }
    public DateTime AddedAt { get; set; } = DateTime.UtcNow;
    public string? AddedBy { get; set; }
    public bool IsEnabled { get; set; } = true;
}

public class CertificateAuditLog
{
    public long Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public CertificateEventType EventType { get; set; }
    public string Thumbprint { get; set; } = string.Empty;
    public string Subject { get; set; } = string.Empty;
    public string? PreviousThumbprint { get; set; }
    public DateTime EventTime { get; set; } = DateTime.UtcNow;
    public string? Details { get; set; }
    public CertificateAcceptanceStrategy StrategyUsed { get; set; }
    public bool IsAccepted { get; set; }
    public string? Operator { get; set; }
}