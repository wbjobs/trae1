using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IAlarmRepository
{
    Task<IEnumerable<Alarm>> GetAllAsync();
    Task<Alarm?> GetByIdAsync(int id);
    Task<IEnumerable<Alarm>> GetActiveAlarmsAsync();
    Task<IEnumerable<Alarm>> GetByDeviceIdAsync(int deviceId);
    Task<Alarm> AddAsync(Alarm alarm);
    Task<Alarm> UpdateAsync(Alarm alarm);
    Task<int> GetActiveAlarmCountAsync();
    Task<IEnumerable<Alarm>> GetByStatusAsync(AlarmStatus status);
}
