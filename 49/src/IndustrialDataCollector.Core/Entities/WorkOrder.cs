using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.Entities;

public class WorkOrder
{
    public int Id { get; set; }
    public string OrderNumber { get; set; } = string.Empty;
    public int? AlarmId { get; set; }
    public int DeviceId { get; set; }
    public string DeviceName { get; set; } = string.Empty;
    public string RegisterName { get; set; } = string.Empty;
    public WorkOrderStatus Status { get; set; } = WorkOrderStatus.Pending;
    public WorkOrderPriority Priority { get; set; } = WorkOrderPriority.Medium;
    public string Title { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public string? AssignedTo { get; set; }
    public DateTime CreatedAt { get; set; } = DateTime.UtcNow;
    public DateTime? StartedAt { get; set; }
    public DateTime? CompletedAt { get; set; }
    public string? Resolution { get; set; }
    public virtual Alarm? Alarm { get; set; }
}
