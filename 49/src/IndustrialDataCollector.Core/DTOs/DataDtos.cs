namespace IndustrialDataCollector.Core.DTOs;

public class DataQueryDto
{
    public string Measurement { get; set; } = "sensor_data";
    public DateTime StartTime { get; set; } = DateTime.UtcNow.AddHours(-1);
    public DateTime EndTime { get; set; } = DateTime.UtcNow;
    public List<int>? DeviceIds { get; set; }
    public List<string>? RegisterNames { get; set; }
    public int Limit { get; set; } = 1000;
}

public class DataPointDto
{
    public string DeviceName { get; set; } = string.Empty;
    public int DeviceId { get; set; }
    public string RegisterName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string? Unit { get; set; }
    public DateTime Timestamp { get; set; }
}

public class OfflineLogDto
{
    public int Id { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public string IpAddress { get; set; } = string.Empty;
    public int Port { get; set; }
    public DateTime OfflineAt { get; set; }
    public DateTime? ReconnectedAt { get; set; }
    public TimeSpan? Duration { get; set; }
    public string Reason { get; set; } = string.Empty;
    public int ReconnectAttempts { get; set; }
    public bool IsResolved { get; set; }
}
