using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface ICertificateManager
{
    CertificateAcceptanceStrategy Strategy { get; set; }

    Task InitializeAsync();

    Task<bool> ValidateCertificateAsync(string deviceId, string deviceName, CertificateInfo certificate);

    Task<bool> IsWhitelistedAsync(string thumbprint, string deviceId);

    Task<CertificateWhitelistEntry> AddToWhitelistAsync(CertificateWhitelistEntry entry);

    Task<bool> RemoveFromWhitelistAsync(long id);

    Task<List<CertificateWhitelistEntry>> GetWhitelistAsync(string? deviceId = null);

    Task<List<CertificateAuditLog>> GetAuditLogsAsync(int hours = 24, string? deviceId = null);

    Task RecordAuditLogAsync(CertificateAuditLog log);

    Task<CertificateAuditLog?> GetLatestCertificateInfoAsync(string deviceId);

    Task CleanupOldAuditLogsAsync();

    event EventHandler<CertificateValidationEventArgs>? CertificateValidated;
    event EventHandler<CertificateChangedEventArgs>? CertificateChanged;
}

public class CertificateValidationEventArgs : EventArgs
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public CertificateInfo Certificate { get; set; } = new();
    public bool IsAccepted { get; set; }
    public CertificateAcceptanceStrategy Strategy { get; set; }
    public string? Reason { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public class CertificateChangedEventArgs : EventArgs
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public CertificateInfo OldCertificate { get; set; } = new();
    public CertificateInfo NewCertificate { get; set; } = new();
    public DateTime ChangeTime { get; set; } = DateTime.UtcNow;
    public string? Reason { get; set; }
}

public interface IEmailNotificationService
{
    Task SendCertificateChangedNotificationAsync(
        string deviceId,
        string deviceName,
        CertificateInfo oldCert,
        CertificateInfo newCert,
        bool isAutoAccepted);

    Task SendCertificateExpiryWarningAsync(
        string deviceId,
        string deviceName,
        CertificateInfo cert,
        int daysUntilExpiry);

    Task SendCertificateRejectedNotificationAsync(
        string deviceId,
        string deviceName,
        CertificateInfo cert,
        string reason);
}