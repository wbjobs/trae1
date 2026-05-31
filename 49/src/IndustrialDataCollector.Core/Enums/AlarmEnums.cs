namespace IndustrialDataCollector.Core.Enums;

public enum AlarmLevel
{
    Info = 0,
    Warning = 1,
    Critical = 2
}

public enum AlarmStatus
{
    Active = 0,
    Acknowledged = 1,
    Resolved = 2
}

public enum WorkOrderStatus
{
    Pending = 0,
    InProgress = 1,
    Completed = 2,
    Cancelled = 3
}

public enum WorkOrderPriority
{
    Low = 0,
    Medium = 1,
    High = 2,
    Critical = 3
}
