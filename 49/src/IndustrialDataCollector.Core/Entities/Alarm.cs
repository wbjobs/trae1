using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.Entities;

public class Alarm
{
    public int Id { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public int RegisterMapId { get; set; }
    public string RegisterName { get; set; } = string.Empty;
    public AlarmLevel Level { get; set; }
    public AlarmStatus Status { get; set; } = AlarmStatus.Active;
    public string Message { get; set; } = string.Empty;
    public double ActualValue { get; set; }
    public double? ThresholdValue { get; set; }
    public string? Unit { get; set; }
    public bool IsAutoControlTriggered { get; set; }
    public string? ControlAction { get; set; }
    public DateTime TriggeredAt { get; set; } = DateTime.UtcNow;
    public DateTime? AcknowledgedAt { get; set; }
    public DateTime? ResolvedAt { get; set; }
    public string? AcknowledgedBy { get; set; }
    public string? AcknowledgedNote { get; set; }
    public int? WorkOrderId { get; set; }
    public virtual WorkOrder? WorkOrder { get; set; }
}
