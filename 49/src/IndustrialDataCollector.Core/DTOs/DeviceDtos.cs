using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.DTOs;

public class DeviceDto
{
    public int Id { get; set; }
    public string Name { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; } = 502;
    public byte SlaveId { get; set; } = 1;
    public DeviceStatus Status { get; set; }
    public string? Description { get; set; }
    public int ReconnectIntervalSeconds { get; set; } = 30;
    public int MaxReconnectAttempts { get; set; } = 5;
    public bool IsActive { get; set; } = true;
    public DateTime? LastConnectedAt { get; set; }
    public DateTime? LastDataReceivedAt { get; set; }
    public List<RegisterMapDto> RegisterMaps { get; set; } = new();
}

public class DeviceCreateDto
{
    public string Name { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; } = 502;
    public byte SlaveId { get; set; } = 1;
    public string? Description { get; set; }
    public int ReconnectIntervalSeconds { get; set; } = 30;
    public int MaxReconnectAttempts { get; set; } = 5;
    public List<RegisterMapCreateDto> RegisterMaps { get; set; } = new();
}

public class DeviceUpdateDto
{
    public string Name { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; } = 502;
    public byte SlaveId { get; set; } = 1;
    public string? Description { get; set; }
    public int ReconnectIntervalSeconds { get; set; } = 30;
    public int MaxReconnectAttempts { get; set; } = 5;
    public bool IsActive { get; set; } = true;
    public List<RegisterMapCreateDto> RegisterMaps { get; set; } = new();
}
