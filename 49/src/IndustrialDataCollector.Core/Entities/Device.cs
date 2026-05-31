using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.Entities;

public class Device
{
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; } = 502;
    public byte SlaveId { get; set; } = 1;
    public DeviceStatus Status { get; set; } = DeviceStatus.Offline;
    public string? Description { get; set; }
    public int ReconnectIntervalSeconds { get; set; } = 30;
    public int MaxReconnectAttempts { get; set; } = 5;
    public int CurrentReconnectAttempts { get; set; }
    public DateTime? LastConnectedAt { get; set; }
    public DateTime? LastDisconnectedAt { get; set; }
    public DateTime? LastDataReceivedAt { get; set; }
    public bool IsActive { get; set; } = true;
    public ICollection<RegisterMap> RegisterMaps { get; set; } = new List<RegisterMap>();
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime? UpdatedAt { get; set; }
}
