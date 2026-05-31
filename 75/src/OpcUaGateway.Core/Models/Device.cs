namespace OpcUaGateway.Core.Models;

public class Device
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public string EndpointUrl { get; set; } = string.Empty;
    public string? Username { get; set; }
    public string? Password { get; set; }
    public bool UseSecurity { get; set; }
    public bool IsEnabled { get; set; } = true;
    public int PollIntervalMs { get; set; } = 100;
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime UpdatedAt { get; set; } = DateTime.UtcNow;
    public List<DataPoint> DataPoints { get; set; } = new();
}

public class DataPoint
{
    public string PointId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string NodeId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public string DataType { get; set; } = "Double";
    public string Unit { get; set; } = string.Empty;
    public string? Description { get; set; }
    public bool IsEnabled { get; set; } = true;
    public double? UpperThreshold { get; set; }
    public double? LowerThreshold { get; set; }
    public double? RateOfChangeThreshold { get; set; }
    public double LastValue { get; set; }
    public DateTime LastUpdateTime { get; set; }
    public Device? Device { get; set; }
}

public enum DeviceStatusType
{
    Online,
    Offline,
    Error,
    Unknown
}

public class DeviceStatus
{
    public string DeviceId { get; set; } = string.Empty;
    public string DeviceName { get; set; } = string.Empty;
    public DeviceStatusType Status { get; set; } = DeviceStatusType.Unknown;
    public DateTime LastUpdateTime { get; set; }
    public DateTime LastSuccessfulCollectTime { get; set; }
    public int SuccessfulCollectCount { get; set; }
    public int FailedCollectCount { get; set; }
    public string? ErrorMessage { get; set; }
    public bool IsConnected { get; set; }
}

public class ThresholdAlert
{
    public string AlertId { get; set; } = string.Empty;
    public string DeviceId { get; set; } = string.Empty;
    public string PointId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public AlertType AlertType { get; set; }
    public double ThresholdValue { get; set; }
    public double ActualValue { get; set; }
    public DateTime TriggerTime { get; set; }
    public bool IsAcknowledged { get; set; }
    public string? Message { get; set; }
}

public enum AlertType
{
    UpperLimit,
    LowerLimit,
    RateOfChange
}

public class MqttMessage
{
    public string MessageId { get; set; } = Guid.NewGuid().ToString();
    public string DeviceId { get; set; } = string.Empty;
    public string PointId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string Unit { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
    public int Quality { get; set; } = 192;
    public string DataType { get; set; } = "Double";

    public string ToJson()
    {
        return System.Text.Json.JsonSerializer.Serialize(this);
    }
}

public class OfflineMessage
{
    public long Id { get; set; }
    public string DeviceId { get; set; } = string.Empty;
    public string PointId { get; set; } = string.Empty;
    public string PointName { get; set; } = string.Empty;
    public double Value { get; set; }
    public string Unit { get; set; } = string.Empty;
    public DateTime Timestamp { get; set; }
    public int Quality { get; set; } = 192;
    public string DataType { get; set; } = "Double";
    public int RetryCount { get; set; }
    public bool IsSent { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime? SentAt { get; set; }

    public MqttMessage ToMqttMessage()
    {
        return new MqttMessage
        {
            MessageId = Guid.NewGuid().ToString(),
            DeviceId = DeviceId,
            PointId = PointId,
            PointName = PointName,
            Value = Value,
            Unit = Unit,
            Timestamp = Timestamp,
            Quality = Quality,
            DataType = DataType
        };
    }
}