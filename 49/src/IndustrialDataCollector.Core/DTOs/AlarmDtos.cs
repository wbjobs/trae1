using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.DTOs;

public class AlarmDto
{
    public int Id { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public int RegisterMapId { get; set; }
    public string RegisterName { get; set; } = string.Empty;
    public AlarmLevel Level { get; set; }
    public AlarmStatus Status { get; set; }
    public string Message { get; set; } = string.Empty;
    public double ActualValue { get; set; }
    public double? ThresholdValue { get; set; }
    public string? Unit { get; set; }
    public bool IsAutoControlTriggered { get; set; }
    public string? ControlAction { get; set; }
    public DateTime TriggeredAt { get; set; }
    public DateTime? AcknowledgedAt { get; set; }
    public DateTime? ResolvedAt { get; set; }
    public string? AcknowledgedBy { get; set; }
    public string? AcknowledgedNote { get; set; }
    public int? WorkOrderId { get; set; }
    public WorkOrderDto? WorkOrder { get; set; }
}

public class AlarmAcknowledgeDto
{
    public string AcknowledgedBy { get; set; } = string.Empty;
    public string? Note { get; set; }
}

public class AlarmResolveDto
{
    public string ResolvedBy { get; set; } = string.Empty;
    public string Resolution { get; set; } = string.Empty;
}

public class WorkOrderDto
{
    public int Id { get; set; }
    public string OrderNumber { get; set; } = string.Empty;
    public int? AlarmId { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public string RegisterName { get; set; } = string.Empty;
    public WorkOrderStatus Status { get; set; }
    public WorkOrderPriority Priority { get; set; }
    public string Title { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public string? AssignedTo { get; set; }
    public DateTime CreatedAt { get; set; }
    public DateTime? StartedAt { get; set; }
    public DateTime? CompletedAt { get; set; }
    public string? Resolution { get; set; }
}

public class WorkOrderUpdateDto
{
    public WorkOrderStatus Status { get; set; }
    public string? AssignedTo { get; set; }
    public string? Resolution { get; set; }
}
