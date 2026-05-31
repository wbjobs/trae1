using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Interfaces;

namespace IndustrialDataCollector.Infrastructure.Repositories;

public class OfflineLogRepository : IOfflineLogRepository
{
    private static readonly List<OfflineLog> _logs = new();
    private static int _nextId = 1;
    private static readonly object _lock = new();

    public Task<IEnumerable<OfflineLog>> GetAllAsync()
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<OfflineLog>>(_logs.OrderByDescending(l => l.OfflineAt).ToList());
        }
    }

    public Task<IEnumerable<OfflineLog>> GetByDeviceIdAsync(int deviceId)
    {
        lock (_lock)
        {
            var deviceLogs = _logs
                .Where(l => l.DeviceId == deviceId)
                .OrderByDescending(l => l.OfflineAt)
                .ToList();
            return Task.FromResult<IEnumerable<OfflineLog>>(deviceLogs);
        }
    }

    public Task<OfflineLog> AddAsync(OfflineLog log)
    {
        lock (_lock)
        {
            log.Id = _nextId++;
            log.CreatedAt = DateTime.UtcNow;
            _logs.Add(log);
            return Task.FromResult(log);
        }
    }

    public Task<OfflineLog> UpdateAsync(OfflineLog log)
    {
        lock (_lock)
        {
            var existing = _logs.FirstOrDefault(l => l.Id == log.Id);
            if (existing == null)
            {
                throw new KeyNotFoundException($"OfflineLog with ID {log.Id} not found");
            }

            existing.ReconnectedAt = log.ReconnectedAt;
            existing.IsResolved = log.IsResolved;
            existing.ReconnectAttempts = log.ReconnectAttempts;
            existing.Reason = log.Reason;

            return Task.FromResult(existing);
        }
    }

    public Task<IEnumerable<OfflineLog>> GetUnresolvedLogsAsync()
    {
        lock (_lock)
        {
            var unresolved = _logs.Where(l => !l.IsResolved).ToList();
            return Task.FromResult<IEnumerable<OfflineLog>>(unresolved);
        }
    }

    public Task<OfflineLog?> GetActiveLogByDeviceIdAsync(int deviceId)
    {
        lock (_lock)
        {
            var activeLog = _logs.FirstOrDefault(l => l.DeviceId == deviceId && !l.IsResolved);
            return Task.FromResult(activeLog);
        }
    }
}
