using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;

namespace IndustrialDataCollector.Infrastructure.Repositories;

public class AlarmRepository : IAlarmRepository
{
    private static readonly List<Alarm> _alarms = new();
    private static int _nextId = 1;
    private static readonly object _lock = new();

    public Task<IEnumerable<Alarm>> GetAllAsync()
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<Alarm>>(
                _alarms.OrderByDescending(a => a.TriggeredAt).ToList());
        }
    }

    public Task<Alarm?> GetByIdAsync(int id)
    {
        lock (_lock)
        {
            return Task.FromResult(_alarms.FirstOrDefault(a => a.Id == id));
        }
    }

    public Task<IEnumerable<Alarm>> GetActiveAlarmsAsync()
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<Alarm>>(
                _alarms.Where(a => a.Status == AlarmStatus.Active)
                       .OrderByDescending(a => a.TriggeredAt).ToList());
        }
    }

    public Task<IEnumerable<Alarm>> GetByDeviceIdAsync(int deviceId)
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<Alarm>>(
                _alarms.Where(a => a.DeviceId == deviceId)
                       .OrderByDescending(a => a.TriggeredAt).ToList());
        }
    }

    public Task<Alarm> AddAsync(Alarm alarm)
    {
        lock (_lock)
        {
            alarm.Id = _nextId++;
            _alarms.Add(alarm);
            return Task.FromResult(alarm);
        }
    }

    public Task<Alarm> UpdateAsync(Alarm alarm)
    {
        lock (_lock)
        {
            var existing = _alarms.FirstOrDefault(a => a.Id == alarm.Id);
            if (existing == null)
                throw new KeyNotFoundException($"Alarm with ID {alarm.Id} not found");

            existing.Status = alarm.Status;
            existing.AcknowledgedAt = alarm.AcknowledgedAt;
            existing.ResolvedAt = alarm.ResolvedAt;
            existing.AcknowledgedBy = alarm.AcknowledgedBy;
            existing.AcknowledgedNote = alarm.AcknowledgedNote;
            existing.WorkOrderId = alarm.WorkOrderId;

            return Task.FromResult(existing);
        }
    }

    public Task<int> GetActiveAlarmCountAsync()
    {
        lock (_lock)
        {
            return Task.FromResult(_alarms.Count(a => a.Status == AlarmStatus.Active));
        }
    }

    public Task<IEnumerable<Alarm>> GetByStatusAsync(AlarmStatus status)
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<Alarm>>(
                _alarms.Where(a => a.Status == status)
                       .OrderByDescending(a => a.TriggeredAt).ToList());
        }
    }
}
