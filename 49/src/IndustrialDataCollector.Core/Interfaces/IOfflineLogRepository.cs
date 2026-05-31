using IndustrialDataCollector.Core.Entities;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IOfflineLogRepository
{
    Task<IEnumerable<OfflineLog>> GetAllAsync();
    Task<IEnumerable<OfflineLog>> GetByDeviceIdAsync(int deviceId);
    Task<OfflineLog> AddAsync(OfflineLog log);
    Task<OfflineLog> UpdateAsync(OfflineLog log);
    Task<IEnumerable<OfflineLog>> GetUnresolvedLogsAsync();
    Task<OfflineLog?> GetActiveLogByDeviceIdAsync(int deviceId);
}
