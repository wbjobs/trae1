using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Api.Services;

public interface IAlarmApiService
{
    Task<IEnumerable<AlarmDto>> GetAllAlarms();
    Task<IEnumerable<AlarmDto>> GetActiveAlarms();
    Task<IEnumerable<AlarmDto>> GetAlarmsByStatus(AlarmStatus status);
    Task<AlarmDto?> GetAlarm(int id);
    Task<(int Active, int Acknowledged, int PendingWorkOrders, int Total)> GetAlarmCounts();
    Task AcknowledgeAlarm(int id, AlarmAcknowledgeDto dto);
    Task ResolveAlarm(int id, AlarmResolveDto dto);
    Task<WorkOrderDto?> GetWorkOrder(int id);
}
