namespace IndustrialDataCollector.Core.Entities;

public class DataPoint
{
    public string Measurement { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public int DeviceId { get; set; }
    public string RegisterName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string? Unit { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
    public Dictionary<string, string> Tags { get; set; } = new();
}
