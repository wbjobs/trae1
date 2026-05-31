using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface IDataCollectionService
{
    Task StartAsync(CancellationToken cancellationToken);
    Task StopAsync();
    Task<IEnumerable<DeviceStatus>> GetDeviceStatusesAsync();
    Task<DeviceStatus?> GetDeviceStatusAsync(string deviceId);
    event EventHandler<DataCollectedEventArgs>? DataCollected;
    event EventHandler<DeviceStatusChangedEventArgs>? DeviceStatusChanged;
}

public class DataCollectedEventArgs : EventArgs
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public List<DataPointReadResult> Results { get; set; } = new();
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public class DataPointReadResult
{
    public string PointId { get; set; } = string.Empty;
    public string NodeId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string Unit { get; set; } = string.Empty;
    public int Quality { get; set; }
    public DateTime Timestamp { get; set; }
}

public class DeviceStatusChangedEventArgs : EventArgs
{
    public string DeviceId { get; set; } = string.Empty;
    public DeviceStatusType NewStatus { get; set; }
    public string? Message { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}