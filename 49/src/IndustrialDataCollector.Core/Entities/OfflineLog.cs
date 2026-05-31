namespace IndustrialDataCollector.Core.Entities;

public class OfflineLog
{
    public int Id { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; }
    public DateTime OfflineAt { get; set; }
    public DateTime? ReconnectedAt { get; set; }
    public TimeSpan? Duration => ReconnectedAt - OfflineAt;
    public string Reason { get; set; } = string.Empty;
    public int ReconnectAttempts { get; set; }
    public bool IsResolved { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
}
