using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Infrastructure.Services;

namespace IndustrialDataCollector.Api.Services;

public class AlarmApiService : IAlarmApiService
{
    private readonly IAlarmRepository _alarmRepository;
    private readonly IWorkOrderRepository _workOrderRepository;
    private readonly AlarmService _alarmService;

    public AlarmApiService(
        IAlarmRepository alarmRepository,
        IWorkOrderRepository workOrderRepository,
        AlarmService alarmService)
    {
        _alarmRepository = alarmRepository;
        _workOrderRepository = workOrderRepository;
        _alarmService = alarmService;
    }

    public async Task<IEnumerable<AlarmDto>> GetAllAlarms()
    {
        var alarms = await _alarmRepository.GetAllAsync();
        return await Task.WhenAll(alarms.Select(MapToDto));
    }

    public async Task<IEnumerable<AlarmDto>> GetActiveAlarms()
    {
        var alarms = await _alarmRepository.GetActiveAlarmsAsync();
        return await Task.WhenAll(alarms.Select(MapToDto));
    }

    public async Task<IEnumerable<AlarmDto>> GetAlarmsByStatus(AlarmStatus status)
    {
        var alarms = await _alarmRepository.GetByStatusAsync(status);
        return await Task.WhenAll(alarms.Select(MapToDto));
    }

    public async Task<AlarmDto?> GetAlarm(int id)
    {
        var alarm = await _alarmRepository.GetByIdAsync(id);
        return alarm != null ? await MapToDto(alarm) : null;
    }

    public async Task<(int Active, int Acknowledged, int PendingWorkOrders, int Total)> GetAlarmCounts()
    {
        var activeCount = await _alarmRepository.GetActiveAlarmCountAsync();
        var acknowledgedCount = (await _alarmRepository.GetByStatusAsync(AlarmStatus.Acknowledged)).Count();
        var pendingWorkOrders = await _workOrderRepository.GetPendingCountAsync();

        return (activeCount, acknowledgedCount, pendingWorkOrders, activeCount + acknowledgedCount);
    }

    public async Task AcknowledgeAlarm(int id, AlarmAcknowledgeDto dto)
    {
        await _alarmService.AcknowledgeAlarmAsync(id, dto.AcknowledgedBy, dto.Note);
    }

    public async Task ResolveAlarm(int id, AlarmResolveDto dto)
    {
        await _alarmService.ResolveAlarmAsync(id, dto.ResolvedBy, dto.Resolution);
    }

    public async Task<WorkOrderDto?> GetWorkOrder(int id)
    {
        var workOrder = await _workOrderRepository.GetByIdAsync(id);
        return workOrder != null ? MapToWorkOrderDto(workOrder) : null;
    }

    private async Task<AlarmDto> MapToDto(Core.Entities.Alarm alarm)
    {
        var dto = new AlarmDto
        {
            Id = alarm.Id,
            DeviceId = alarm.DeviceId,
            DeviceName = alarm.DeviceName,
            RegisterMapId = alarm.RegisterMapId,
            RegisterName = alarm.RegisterName,
            Level = alarm.Level,
            Status = alarm.Status,
            Message = alarm.Message,
            ActualValue = alarm.ActualValue,
            ThresholdValue = alarm.ThresholdValue,
            Unit = alarm.Unit,
            IsAutoControlTriggered = alarm.IsAutoControlTriggered,
            ControlAction = alarm.ControlAction,
            TriggeredAt = alarm.TriggeredAt,
            AcknowledgedAt = alarm.AcknowledgedAt,
            ResolvedAt = alarm.ResolvedAt,
            AcknowledgedBy = alarm.AcknowledgedBy,
            AcknowledgedNote = alarm.AcknowledgedNote,
            WorkOrderId = alarm.WorkOrderId
        };

        if (alarm.WorkOrderId.HasValue)
        {
            var workOrder = await _workOrderRepository.GetByIdAsync(alarm.WorkOrderId.Value);
            if (workOrder != null)
            {
                dto.WorkOrder = MapToWorkOrderDto(workOrder);
            }
        }

        return dto;
    }

    private WorkOrderDto MapToWorkOrderDto(Core.Entities.WorkOrder workOrder)
    {
        return new WorkOrderDto
        {
            Id = workOrder.Id,
            OrderNumber = workOrder.OrderNumber,
            AlarmId = workOrder.AlarmId,
            DeviceId = workOrder.DeviceId,
            DeviceName = workOrder.DeviceName,
            RegisterName = workOrder.RegisterName,
            Status = workOrder.Status,
            Priority = workOrder.Priority,
            Title = workOrder.Title,
            Description = workOrder.Description,
            AssignedTo = workOrder.AssignedTo,
            CreatedAt = workOrder.CreatedAt,
            StartedAt = workOrder.StartedAt,
            CompletedAt = workOrder.CompletedAt,
            Resolution = workOrder.Resolution
        };
    }
}
