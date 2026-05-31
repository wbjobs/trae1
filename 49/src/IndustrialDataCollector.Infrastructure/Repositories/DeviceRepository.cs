using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;

namespace IndustrialDataCollector.Infrastructure.Repositories;

public class DeviceRepository : IDeviceRepository
{
    private static readonly List<Device> _devices = new();
    private static int _nextId = 1;
    private static readonly object _lock = new();

    public Task<IEnumerable<Device>> GetAllAsync()
    {
        lock (_lock)
        {
            return Task.FromResult<IEnumerable<Device>>(_devices.ToList());
        }
    }

    public Task<Device?> GetByIdAsync(int id)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.Id == id);
            return Task.FromResult(device);
        }
    }

    public Task<Device> AddAsync(Device device)
    {
        lock (_lock)
        {
            device.Id = _nextId++;
            device.CreatedAt = DateTime.UtcNow;
            _devices.Add(device);
            return Task.FromResult(device);
        }
    }

    public Task<Device> UpdateAsync(Device device)
    {
        lock (_lock)
        {
            var existing = _devices.FirstOrDefault(d => d.Id == device.Id);
            if (existing == null)
            {
                throw new KeyNotFoundException($"Device with ID {device.Id} not found");
            }

            existing.Name = device.Name;
            existing.IpAddress = device.IpAddress;
            existing.Port = device.Port;
            existing.SlaveId = device.SlaveId;
            existing.Description = device.Description;
            existing.ReconnectIntervalSeconds = device.ReconnectIntervalSeconds;
            existing.MaxReconnectAttempts = device.MaxReconnectAttempts;
            existing.IsActive = device.IsActive;
            existing.RegisterMaps = device.RegisterMaps;
            existing.UpdatedAt = DateTime.UtcNow;

            return Task.FromResult(existing);
        }
    }

    public Task DeleteAsync(int id)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.Id == id);
            if (device != null)
            {
                _devices.Remove(device);
            }
            return Task.CompletedTask;
        }
    }

    public Task<IEnumerable<Device>> GetActiveDevicesAsync()
    {
        lock (_lock)
        {
            var activeDevices = _devices.Where(d => d.IsActive).ToList();
            return Task.FromResult<IEnumerable<Device>>(activeDevices);
        }
    }

    public Task UpdateStatusAsync(int deviceId, DeviceStatus status, DateTime? timestamp = null)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.Id == deviceId);
            if (device != null)
            {
                device.Status = status;
                device.UpdatedAt = timestamp ?? DateTime.UtcNow;

                if (status == DeviceStatus.Online)
                {
                    device.LastConnectedAt = timestamp ?? DateTime.UtcNow;
                    device.CurrentReconnectAttempts = 0;
                }
                else if (status == DeviceStatus.Offline)
                {
                    device.LastDisconnectedAt = timestamp ?? DateTime.UtcNow;
                }
            }
            return Task.CompletedTask;
        }
    }
}
