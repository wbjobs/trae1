using Dapper;
using Microsoft.Data.Sqlite;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Application.Services;

public class CertificateManagerService : ICertificateManager
{
    private readonly ILogger<CertificateManagerService> _logger;
    private readonly GatewayConfig _config;
    private readonly string _connectionString;
    private readonly object _lock = new();
    private List<CertificateWhitelistEntry> _whitelistCache = new();
    private DateTime _lastWhitelistRefresh = DateTime.MinValue;

    public CertificateAcceptanceStrategy Strategy { get; set; }

    public event EventHandler<CertificateValidationEventArgs>? CertificateValidated;
    public event EventHandler<CertificateChangedEventArgs>? CertificateChanged;

    public CertificateManagerService(
        ILogger<CertificateManagerService> logger,
        IOptions<GatewayConfig> config)
    {
        _logger = logger;
        _config = config.Value;
        _connectionString = $"Data Source={_config.DatabasePath}";
        Strategy = _config.Certificate.AcceptanceStrategy;
    }

    public async Task InitializeAsync()
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                CREATE TABLE IF NOT EXISTS CertificateWhitelist (
                    Id INTEGER PRIMARY KEY AUTOINCREMENT,
                    Thumbprint TEXT NOT NULL UNIQUE,
                    Subject TEXT NOT NULL,
                    Issuer TEXT NOT NULL,
                    NotAfter TEXT NOT NULL,
                    DeviceId TEXT NOT NULL,
                    Description TEXT,
                    AddedAt TEXT NOT NULL,
                    AddedBy TEXT,
                    IsEnabled INTEGER DEFAULT 1
                );
            ");

            await connection.ExecuteAsync(@"
                CREATE TABLE IF NOT EXISTS CertificateAuditLog (
                    Id INTEGER PRIMARY KEY AUTOINCREMENT,
                    DeviceId TEXT NOT NULL,
                    DeviceName TEXT NOT NULL,
                    EventType TEXT NOT NULL,
                    Thumbprint TEXT NOT NULL,
                    Subject TEXT NOT NULL,
                    PreviousThumbprint TEXT,
                    EventTime TEXT NOT NULL,
                    Details TEXT,
                    StrategyUsed TEXT NOT NULL,
                    IsAccepted INTEGER DEFAULT 0,
                    Operator TEXT
                );
            ");

            _logger.LogInformation("Certificate manager initialized with strategy: {Strategy}", Strategy);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to initialize certificate manager");
            throw;
        }
    }

    public async Task<bool> ValidateCertificateAsync(string deviceId, string deviceName, CertificateInfo certificate)
    {
        bool isAccepted = false;
        string? reason = null;

        switch (Strategy)
        {
            case CertificateAcceptanceStrategy.AutoTrustAll:
                isAccepted = true;
                reason = "Auto-trusted by policy";
                break;

            case CertificateAcceptanceStrategy.WhitelistOnly:
                isAccepted = await IsWhitelistedAsync(certificate.Thumbprint, deviceId);
                reason = isAccepted ? "Whitelisted" : "Not in whitelist";
                break;

            case CertificateAcceptanceStrategy.ManualConfirmation:
                isAccepted = await IsWhitelistedAsync(certificate.Thumbprint, deviceId);
                reason = isAccepted ? "Manually confirmed" : "Pending manual confirmation";
                break;
        }

        var auditLog = new CertificateAuditLog
        {
            DeviceId = deviceId,
            DeviceName = deviceName,
            EventType = isAccepted ? CertificateEventType.CertificateAccepted : CertificateEventType.CertificateRejected,
            Thumbprint = certificate.Thumbprint,
            Subject = certificate.Subject,
            EventTime = DateTime.UtcNow,
            Details = reason,
            StrategyUsed = Strategy,
            IsAccepted = isAccepted
        };

        await RecordAuditLogAsync(auditLog);

        if (certificate.IsExpired)
        {
            _logger.LogWarning("Certificate {Thumbprint} for {DeviceId} is expired (expiry: {Expiry})",
                certificate.Thumbprint, deviceId, certificate.NotAfter);
        }
        else if (certificate.DaysUntilExpiry <= _config.Certificate.ExpiryWarningDays)
        {
            _logger.LogWarning("Certificate {Thumbprint} for {DeviceId} expires in {Days} days",
                certificate.Thumbprint, deviceId, certificate.DaysUntilExpiry);
        }

        CertificateValidated?.Invoke(this, new CertificateValidationEventArgs
        {
            DeviceId = deviceId,
            DeviceName = deviceName,
            Certificate = certificate,
            IsAccepted = isAccepted,
            Strategy = Strategy,
            Reason = reason
        });

        return isAccepted;
    }

    public async Task<bool> IsWhitelistedAsync(string thumbprint, string deviceId)
    {
        await RefreshWhitelistCacheAsync();

        lock (_lock)
        {
            return _whitelistCache.Any(w =>
                w.Thumbprint.Equals(thumbprint, StringComparison.OrdinalIgnoreCase) &&
                w.DeviceId.Equals(deviceId, StringComparison.OrdinalIgnoreCase) &&
                w.IsEnabled);
        }
    }

    public async Task<CertificateWhitelistEntry> AddToWhitelistAsync(CertificateWhitelistEntry entry)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var exists = await connection.ExecuteScalarAsync<long?>(
                "SELECT Id FROM CertificateWhitelist WHERE Thumbprint = @Thumbprint AND DeviceId = @DeviceId",
                new { entry.Thumbprint, entry.DeviceId });

            if (exists.HasValue)
            {
                await connection.ExecuteAsync(@"
                    UPDATE CertificateWhitelist SET
                        Subject = @Subject,
                        Issuer = @Issuer,
                        NotAfter = @NotAfter,
                        Description = @Description,
                        IsEnabled = @IsEnabled
                    WHERE Id = @Id",
                    new
                    {
                        entry.Subject,
                        entry.Issuer,
                        entry.NotAfter,
                        entry.Description,
                        entry.IsEnabled,
                        Id = exists.Value
                    });

                entry.Id = exists.Value;
            }
            else
            {
                entry.Id = await connection.ExecuteScalarAsync<long>(@"
                    INSERT INTO CertificateWhitelist
                    (Thumbprint, Subject, Issuer, NotAfter, DeviceId, Description, AddedAt, AddedBy, IsEnabled)
                    VALUES (@Thumbprint, @Subject, @Issuer, @NotAfter, @DeviceId, @Description, @AddedAt, @AddedBy, @IsEnabled);
                    SELECT last_insert_rowid();",
                    entry);
            }

            _lastWhitelistRefresh = DateTime.MinValue;

            _logger.LogInformation("Certificate {Thumbprint} added to whitelist for {DeviceId}",
                entry.Thumbprint, entry.DeviceId);

            return entry;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to add certificate to whitelist");
            throw;
        }
    }

    public async Task<bool> RemoveFromWhitelistAsync(long id)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var affected = await connection.ExecuteAsync(
                "DELETE FROM CertificateWhitelist WHERE Id = @Id",
                new { Id = id });

            _lastWhitelistRefresh = DateTime.MinValue;

            return affected > 0;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to remove certificate from whitelist");
            return false;
        }
    }

    public async Task<List<CertificateWhitelistEntry>> GetWhitelistAsync(string? deviceId = null)
    {
        await RefreshWhitelistCacheAsync();

        lock (_lock)
        {
            return deviceId == null
                ? _whitelistCache.ToList()
                : _whitelistCache.Where(w => w.DeviceId.Equals(deviceId, StringComparison.OrdinalIgnoreCase)).ToList();
        }
    }

    public async Task<List<CertificateAuditLog>> GetAuditLogsAsync(int hours = 24, string? deviceId = null)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var cutoff = DateTime.UtcNow.AddHours(-hours);

            IEnumerable<CertificateAuditLog> logs;

            if (deviceId != null)
            {
                logs = await connection.QueryAsync<CertificateAuditLog>(@"
                    SELECT * FROM CertificateAuditLog
                    WHERE EventTime >= @Cutoff AND DeviceId = @DeviceId
                    ORDER BY EventTime DESC",
                    new { Cutoff = cutoff, DeviceId = deviceId });
            }
            else
            {
                logs = await connection.QueryAsync<CertificateAuditLog>(@"
                    SELECT * FROM CertificateAuditLog
                    WHERE EventTime >= @Cutoff
                    ORDER BY EventTime DESC",
                    new { Cutoff = cutoff });
            }

            return logs.ToList();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to retrieve audit logs");
            return new List<CertificateAuditLog>();
        }
    }

    public async Task RecordAuditLogAsync(CertificateAuditLog log)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            await connection.ExecuteAsync(@"
                INSERT INTO CertificateAuditLog
                (DeviceId, DeviceName, EventType, Thumbprint, Subject, PreviousThumbprint, EventTime, Details, StrategyUsed, IsAccepted, Operator)
                VALUES (@DeviceId, @DeviceName, @EventType, @Thumbprint, @Subject, @PreviousThumbprint, @EventTime, @Details, @StrategyUsed, @IsAccepted, @Operator);
            ", log);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to record certificate audit log");
        }
    }

    public async Task<CertificateAuditLog?> GetLatestCertificateInfoAsync(string deviceId)
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var log = await connection.QueryFirstOrDefaultAsync<CertificateAuditLog>(@"
                SELECT * FROM CertificateAuditLog
                WHERE DeviceId = @DeviceId
                ORDER BY EventTime DESC
                LIMIT 1",
                new { DeviceId = deviceId });

            return log;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to retrieve latest certificate info for {DeviceId}", deviceId);
            return null;
        }
    }

    public async Task CleanupOldAuditLogsAsync()
    {
        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var cutoff = DateTime.UtcNow.AddDays(-_config.Certificate.AuditLogRetentionDays);
            var deleted = await connection.ExecuteAsync(
                "DELETE FROM CertificateAuditLog WHERE EventTime < @Cutoff",
                new { Cutoff = cutoff });

            if (deleted > 0)
            {
                _logger.LogInformation("Cleaned up {Count} old certificate audit logs", deleted);
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to cleanup old audit logs");
        }
    }

    private async Task RefreshWhitelistCacheAsync()
    {
        if ((DateTime.UtcNow - _lastWhitelistRefresh).TotalSeconds < 30) return;

        try
        {
            using var connection = new SqliteConnection(_connectionString);
            await connection.OpenAsync();

            var entries = await connection.QueryAsync<CertificateWhitelistEntry>(
                "SELECT * FROM CertificateWhitelist");

            lock (_lock)
            {
                _whitelistCache = entries.ToList();
                _lastWhitelistRefresh = DateTime.UtcNow;
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to refresh whitelist cache");
        }
    }
}