using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;
using Microsoft.Extensions.Logging;

namespace IndustrialDataCollector.Infrastructure.Services;

public class AlarmService
{
    private readonly IAlarmRepository _alarmRepository;
    private readonly IWorkOrderRepository _workOrderRepository;
    private readonly ILogger<AlarmService> _logger;
    private readonly Dictionary<int, DateTime> _lastAlarmTime = new();
    private readonly object _lock = new();
    private const int AlarmCooldownSeconds = 60;

    public AlarmService(
        IAlarmRepository alarmRepository,
        IWorkOrderRepository workOrderRepository,
        ILogger<AlarmService> logger)
    {
        _alarmRepository = alarmRepository;
        _workOrderRepository = workOrderRepository;
        _logger = logger;
    }

    public async Task CheckAndProcessAlarmsAsync(
        Device device,
        RegisterMap registerMap,
        double value,
        ModbusService modbusService)
    {
        if (!registerMap.EnableAlarm) return;

        var isUpperExceeded = registerMap.UpperThreshold.HasValue && value > registerMap.UpperThreshold.Value;
        var isLowerExceeded = registerMap.LowerThreshold.HasValue && value < registerMap.LowerThreshold.Value;

        if (!isUpperExceeded && !isLowerExceeded)
        {
            await CheckAndResolveAlarmAsync(device, registerMap, value);
            return;
        }

        lock (_lock)
        {
            if (_lastAlarmTime.TryGetValue(registerMap.Id, out var lastTime))
            {
                if (DateTime.UtcNow - lastTime < TimeSpan.FromSeconds(AlarmCooldownSeconds))
                {
                    return;
                }
            }
            _lastAlarmTime[registerMap.Id] = DateTime.UtcNow;
        }

        var thresholdValue = isUpperExceeded ? registerMap.UpperThreshold : registerMap.LowerThreshold;
        var alarmLevel = (isUpperExceeded && value > registerMap.UpperThreshold * 1.2) ||
                         (isLowerExceeded && value < registerMap.LowerThreshold * 0.8)
                         ? AlarmLevel.Critical
                         : AlarmLevel.Warning;

        var alarm = new Alarm
        {
            DeviceId = device.Id,
            DeviceName = device.Name,
            RegisterMapId = registerMap.Id,
            RegisterName = registerMap.Name,
            Level = alarmLevel,
            Status = AlarmStatus.Active,
            Message = BuildAlarmMessage(registerMap, value, isUpperExceeded),
            ActualValue = value,
            ThresholdValue = thresholdValue,
            Unit = registerMap.Unit,
            TriggeredAt = DateTime.UtcNow
        };

        alarm = await _alarmRepository.AddAsync(alarm);
        _logger.LogWarning("Alarm triggered: {Message} (Value: {Value}{Unit}, Threshold: {Threshold})",
            alarm.Message, value, registerMap.Unit, thresholdValue);

        if (registerMap.ControlCoilAddress.HasValue)
        {
            try
            {
                await modbusService.WriteSingleCoilAsync(
                    registerMap.ControlCoilAddress.Value,
                    registerMap.ControlCoilValue);

                alarm.IsAutoControlTriggered = true;
                alarm.ControlAction = $"Write coil {registerMap.ControlCoilAddress} = {registerMap.ControlCoilValue}";
                await _alarmRepository.UpdateAsync(alarm);

                _logger.LogInformation("Auto control executed for {DeviceName}: {ControlAction}",
                    device.Name, alarm.ControlAction);
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Auto control failed for {DeviceName}: {ControlDescription}",
                    device.Name, registerMap.ControlDescription);
                alarm.ControlAction = $"Failed: {ex.Message}";
                await _alarmRepository.UpdateAsync(alarm);
            }
        }

        var workOrder = new WorkOrder
        {
            AlarmId = alarm.Id,
            DeviceId = device.Id,
            DeviceName = device.Name,
            RegisterName = registerMap.Name,
            Status = WorkOrderStatus.Pending,
            Priority = alarmLevel == AlarmLevel.Critical ? WorkOrderPriority.Critical : WorkOrderPriority.High,
            Title = $"[{alarmLevel}] {registerMap.Name} 告警 - {device.Name}",
            Description = BuildWorkOrderDescription(registerMap, value, isUpperExceeded, alarm),
            CreatedAt = DateTime.UtcNow
        };

        workOrder = await _workOrderRepository.AddAsync(workOrder);
        alarm.WorkOrderId = workOrder.Id;
        await _alarmRepository.UpdateAsync(alarm);

        _logger.LogInformation("Work order created: {OrderNumber} for alarm {AlarmId}",
            workOrder.OrderNumber, alarm.Id);
    }

    private async Task CheckAndResolveAlarmAsync(
        Device device,
        RegisterMap registerMap,
        double value)
    {
        var activeAlarms = await _alarmRepository.GetActiveAlarmsAsync();
        var matchingAlarms = activeAlarms
            .Where(a => a.DeviceId == device.Id && a.RegisterName == registerMap.Name)
            .ToList();

        foreach (var alarm in matchingAlarms)
        {
            alarm.Status = AlarmStatus.Resolved;
            alarm.ResolvedAt = DateTime.UtcNow;
            alarm.AcknowledgedBy = "System";
            alarm.AcknowledgedNote = $"Value returned to normal range: {value}{registerMap.Unit}";

            await _alarmRepository.UpdateAsync(alarm);
            _logger.LogInformation("Alarm {AlarmId} auto-resolved. Value: {Value}{Unit}",
                alarm.Id, value, registerMap.Unit);

            if (alarm.WorkOrderId.HasValue)
            {
                var workOrder = await _workOrderRepository.GetByIdAsync(alarm.WorkOrderId.Value);
                if (workOrder != null && workOrder.Status == WorkOrderStatus.Pending)
                {
                    workOrder.Status = WorkOrderStatus.Completed;
                    workOrder.CompletedAt = DateTime.UtcNow;
                    workOrder.Resolution = $"Auto-resolved. Value returned to normal: {value}{registerMap.Unit}";
                    await _workOrderRepository.UpdateAsync(workOrder);
                }
            }
        }
    }

    private string BuildAlarmMessage(RegisterMap registerMap, double value, bool isUpperExceeded)
    {
        if (isUpperExceeded)
        {
            return $"{registerMap.Name} exceeded upper threshold {registerMap.UpperThreshold}{registerMap.Unit}, current value: {value}{registerMap.Unit}";
        }
        return $"{registerMap.Name} below lower threshold {registerMap.LowerThreshold}{registerMap.Unit}, current value: {value}{registerMap.Unit}";
    }

    private string BuildWorkOrderDescription(RegisterMap registerMap, double value, bool isUpperExceeded, Alarm alarm)
    {
        var description = $"设备 {alarm.DeviceName} 的 {registerMap.Name} 发生告警。\n" +
                         $"当前值: {value}{registerMap.Unit}\n" +
                         $"阈值: {(isUpperExceeded ? $"上限 {registerMap.UpperThreshold}" : $"下限 {registerMap.LowerThreshold}")}{registerMap.Unit}\n" +
                         $"告警级别: {alarm.Level}\n" +
                         $"触发时间: {alarm.TriggeredAt:yyyy-MM-dd HH:mm:ss}";

        if (!string.IsNullOrEmpty(registerMap.ControlDescription))
        {
            description += $"\n联动控制: " + registerMap.ControlDescription;
        }

        return description;
    }

    public async Task<AcknowledgeResult> AcknowledgeAlarmAsync(int alarmId, string acknowledgedBy, string? note)
    {
        var alarm = await _alarmRepository.GetByIdAsync(alarmId);
        if (alarm == null)
        {
            return AcknowledgeResult.NotFound();
        }

        if (alarm.Status != AlarmStatus.Active)
        {
            return AcknowledgeResult.AlreadyHandled(alarm.Status);
        }

        alarm.Status = AlarmStatus.Acknowledged;
        alarm.AcknowledgedAt = DateTime.UtcNow;
        alarm.AcknowledgedBy = acknowledgedBy;
        alarm.AcknowledgedNote = note;

        await _alarmRepository.UpdateAsync(alarm);

        if (alarm.WorkOrderId.HasValue)
        {
            var workOrder = await _workOrderRepository.GetByIdAsync(alarm.WorkOrderId.Value);
            if (workOrder != null && workOrder.Status == WorkOrderStatus.Pending)
            {
                workOrder.Status = WorkOrderStatus.InProgress;
                workOrder.StartedAt = DateTime.UtcNow;
                workOrder.AssignedTo = acknowledgedBy;
                await _workOrderRepository.UpdateAsync(workOrder);
            }
        }

        _logger.LogInformation("Alarm {AlarmId} acknowledged by {User}", alarmId, acknowledgedBy);
        return AcknowledgeResult.Success();
    }

    public async Task<ResolveResult> ResolveAlarmAsync(int alarmId, string resolvedBy, string resolution)
    {
        var alarm = await _alarmRepository.GetByIdAsync(alarmId);
        if (alarm == null)
        {
            return ResolveResult.NotFound();
        }

        alarm.Status = AlarmStatus.Resolved;
        alarm.ResolvedAt = DateTime.UtcNow;
        alarm.AcknowledgedBy = resolvedBy;
        alarm.AcknowledgedNote = resolution;

        await _alarmRepository.UpdateAsync(alarm);

        if (alarm.WorkOrderId.HasValue)
        {
            var workOrder = await _workOrderRepository.GetByIdAsync(alarm.WorkOrderId.Value);
            if (workOrder != null)
            {
                workOrder.Status = WorkOrderStatus.Completed;
                workOrder.CompletedAt = DateTime.UtcNow;
                workOrder.Resolution = resolution;
                await _workOrderRepository.UpdateAsync(workOrder);
            }
        }

        _logger.LogInformation("Alarm {AlarmId} resolved by {User}", alarmId, resolvedBy);
        return ResolveResult.Success();
    }
}

public class AcknowledgeResult
{
    public bool Success { get; set; }
    public string Message { get; set; } = string.Empty;
    public AlarmStatus? CurrentStatus { get; set; }

    public static AcknowledgeResult Success() =>
        new() { Success = true, Message = "告警已确认" };

    public static AcknowledgeResult NotFound() =>
        new() { Success = false, Message = "告警不存在" };

    public static AcknowledgeResult AlreadyHandled(AlarmStatus status) =>
        new() { Success = false, Message = $"告警已处理，当前状态: {status}", CurrentStatus = status };
}

public class ResolveResult
{
    public bool Success { get; set; }
    public string Message { get; set; } = string.Empty;

    public static ResolveResult Success() =>
        new() { Success = true, Message = "告警已消除" };

    public static ResolveResult NotFound() =>
        new() { Success = false, Message = "告警不存在" };
}
