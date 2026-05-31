using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Infrastructure.Services;
using Microsoft.AspNetCore.Mvc;

namespace IndustrialDataCollector.Api.Controllers;

[ApiController]
[Route("api/[controller]")]
public class AlarmsController : ControllerBase
{
    private readonly IAlarmRepository _alarmRepository;
    private readonly IWorkOrderRepository _workOrderRepository;
    private readonly AlarmService _alarmService;

    public AlarmsController(
        IAlarmRepository alarmRepository,
        IWorkOrderRepository workOrderRepository,
        AlarmService alarmService)
    {
        _alarmRepository = alarmRepository;
        _workOrderRepository = workOrderRepository;
        _alarmService = alarmService;
    }

    [HttpGet]
    public async Task<ActionResult<IEnumerable<AlarmDto>>> GetAlarms(
        [FromQuery] AlarmStatus? status,
        [FromQuery] int? deviceId)
    {
        IEnumerable<Core.Entities.Alarm> alarms;

        if (status.HasValue)
        {
            alarms = await _alarmRepository.GetByStatusAsync(status.Value);
        }
        else if (deviceId.HasValue)
        {
            alarms = await _alarmRepository.GetByDeviceIdAsync(deviceId.Value);
        }
        else
        {
            alarms = await _alarmRepository.GetAllAsync();
        }

        var dtos = await Task.WhenAll(alarms.Select(MapToDto));
        return Ok(dtos);
    }

    [HttpGet("active")]
    public async Task<ActionResult<IEnumerable<AlarmDto>>> GetActiveAlarms()
    {
        var alarms = await _alarmRepository.GetActiveAlarmsAsync();
        var dtos = await Task.WhenAll(alarms.Select(MapToDto));
        return Ok(dtos);
    }

    [HttpGet("count")]
    public async Task<ActionResult<object>> GetAlarmCounts()
    {
        var activeCount = await _alarmRepository.GetActiveAlarmCountAsync();
        var acknowledgedCount = (await _alarmRepository.GetByStatusAsync(AlarmStatus.Acknowledged)).Count();
        var pendingWorkOrders = await _workOrderRepository.GetPendingCountAsync();

        return Ok(new
        {
            Active = activeCount,
            Acknowledged = acknowledgedCount,
            PendingWorkOrders = pendingWorkOrders,
            Total = activeCount + acknowledgedCount
        });
    }

    [HttpGet("{id}")]
    public async Task<ActionResult<AlarmDto>> GetAlarm(int id)
    {
        var alarm = await _alarmRepository.GetByIdAsync(id);
        if (alarm == null) return NotFound();

        return Ok(await MapToDto(alarm));
    }

    [HttpPost("{id}/acknowledge")]
    public async Task<ActionResult<AlarmDto>> AcknowledgeAlarm(int id, [FromBody] AlarmAcknowledgeDto dto)
    {
        var result = await _alarmService.AcknowledgeAlarmAsync(id, dto.AcknowledgedBy, dto.Note);
        if (!result.Success)
        {
            return BadRequest(new { message = result.Message });
        }

        var alarm = await _alarmRepository.GetByIdAsync(id);
        return Ok(await MapToDto(alarm!));
    }

    [HttpPost("{id}/resolve")]
    public async Task<ActionResult<AlarmDto>> ResolveAlarm(int id, [FromBody] AlarmResolveDto dto)
    {
        var result = await _alarmService.ResolveAlarmAsync(id, dto.ResolvedBy, dto.Resolution);
        if (!result.Success)
        {
            return BadRequest(new { message = result.Message });
        }

        var alarm = await _alarmRepository.GetByIdAsync(id);
        return Ok(await MapToDto(alarm!));
    }

    [HttpGet("workorders")]
    public async Task<ActionResult<IEnumerable<WorkOrderDto>>> GetWorkOrders(
        [FromQuery] WorkOrderStatus? status,
        [FromQuery] int? deviceId)
    {
        IEnumerable<Core.Entities.WorkOrder> workOrders;

        if (status.HasValue)
        {
            workOrders = await _workOrderRepository.GetByStatusAsync(status.Value);
        }
        else if (deviceId.HasValue)
        {
            workOrders = await _workOrderRepository.GetByDeviceIdAsync(deviceId.Value);
        }
        else
        {
            workOrders = await _workOrderRepository.GetAllAsync();
        }

        return Ok(workOrders.Select(MapToWorkOrderDto));
    }

    [HttpGet("workorders/{id}")]
    public async Task<ActionResult<WorkOrderDto>> GetWorkOrder(int id)
    {
        var workOrder = await _workOrderRepository.GetByIdAsync(id);
        if (workOrder == null) return NotFound();

        return Ok(MapToWorkOrderDto(workOrder));
    }

    [HttpPut("workorders/{id}")]
    public async Task<ActionResult<WorkOrderDto>> UpdateWorkOrder(int id, [FromBody] WorkOrderUpdateDto dto)
    {
        var workOrder = await _workOrderRepository.GetByIdAsync(id);
        if (workOrder == null) return NotFound();

        workOrder.Status = dto.Status;
        workOrder.AssignedTo = dto.AssignedTo;
        if (dto.Resolution != null)
        {
            workOrder.Resolution = dto.Resolution;
        }

        if (dto.Status == WorkOrderStatus.InProgress && workOrder.StartedAt == null)
        {
            workOrder.StartedAt = DateTime.UtcNow;
        }

        if (dto.Status == WorkOrderStatus.Completed)
        {
            workOrder.CompletedAt = DateTime.UtcNow;
        }

        var updated = await _workOrderRepository.UpdateAsync(workOrder);
        return Ok(MapToWorkOrderDto(updated));
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
