using IndustrialDataCollector.Core.Entities;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IDeviceRepository
{
    Task<IEnumerable<Device>> GetAllAsync();
    Task<Device?> GetByIdAsync(int id);
    Task<Device> AddAsync(Device device);
    Task<Device> UpdateAsync(Device device);
    Task DeleteAsync(int id);
    Task<IEnumerable<Device>> GetActiveDevicesAsync();
    Task UpdateStatusAsync(int deviceId, DeviceStatus status, DateTime? timestamp = null);
}
