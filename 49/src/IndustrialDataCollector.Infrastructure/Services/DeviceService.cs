using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Interfaces;

namespace IndustrialDataCollector.Infrastructure.Services;

public class DeviceService
{
    private readonly IDeviceRepository _deviceRepository;
    private readonly IOfflineLogRepository _offlineLogRepository;

    public DeviceService(
        IDeviceRepository deviceRepository,
        IOfflineLogRepository offlineLogRepository)
    {
        _deviceRepository = deviceRepository;
        _offlineLogRepository = offlineLogRepository;
    }

    public async Task<IEnumerable<DeviceDto>> GetAllDevicesAsync()
    {
        var devices = await _deviceRepository.GetAllAsync();
        return devices.Select(MapToDto);
    }

    public async Task<DeviceDto?> GetDeviceByIdAsync(int id)
    {
        var device = await _deviceRepository.GetByIdAsync(id);
        return device != null ? MapToDto(device) : null;
    }

    public async Task<DeviceDto> AddDeviceAsync(DeviceCreateDto dto)
    {
        var device = new Device
        {
            Name = dto.Name,
            IpAddress = dto.IpAddress,
            Port = dto.Port,
            SlaveId = dto.SlaveId,
            Description = dto.Description,
            ReconnectIntervalSeconds = dto.ReconnectIntervalSeconds,
            MaxReconnectAttempts = dto.MaxReconnectAttempts,
            IsActive = true,
            RegisterMaps = dto.RegisterMaps.Select(r => new RegisterMap
            {
                Name = r.Name,
                Description = r.Description,
                RegisterType = r.RegisterType,
                Address = r.Address,
                Length = r.Length,
                ScaleFactor = r.ScaleFactor,
                Offset = r.Offset,
                Unit = r.Unit,
                DataType = r.DataType,
                IsActive = r.IsActive,
                UpperThreshold = r.UpperThreshold,
                LowerThreshold = r.LowerThreshold,
                EnableAlarm = r.EnableAlarm,
                ControlCoilAddress = r.ControlCoilAddress,
                ControlCoilValue = r.ControlCoilValue,
                ControlDescription = r.ControlDescription
            }).ToList()
        };

        var created = await _deviceRepository.AddAsync(device);
        return MapToDto(created);
    }

    public async Task<DeviceDto?> UpdateDeviceAsync(int id, DeviceUpdateDto dto)
    {
        var existing = await _deviceRepository.GetByIdAsync(id);
        if (existing == null) return null;

        existing.Name = dto.Name;
        existing.IpAddress = dto.IpAddress;
        existing.Port = dto.Port;
        existing.SlaveId = dto.SlaveId;
        existing.Description = dto.Description;
        existing.ReconnectIntervalSeconds = dto.ReconnectIntervalSeconds;
        existing.MaxReconnectAttempts = dto.MaxReconnectAttempts;
        existing.IsActive = dto.IsActive;
        existing.RegisterMaps = dto.RegisterMaps.Select(r => new RegisterMap
        {
            Name = r.Name,
            Description = r.Description,
            RegisterType = r.RegisterType,
            Address = r.Address,
            Length = r.Length,
            ScaleFactor = r.ScaleFactor,
            Offset = r.Offset,
            Unit = r.Unit,
            DataType = r.DataType,
            IsActive = r.IsActive,
            UpperThreshold = r.UpperThreshold,
            LowerThreshold = r.LowerThreshold,
            EnableAlarm = r.EnableAlarm,
            ControlCoilAddress = r.ControlCoilAddress,
            ControlCoilValue = r.ControlCoilValue,
            ControlDescription = r.ControlDescription
        }).ToList();

        var updated = await _deviceRepository.UpdateAsync(existing);
        return MapToDto(updated);
    }

    public async Task<bool> DeleteDeviceAsync(int id)
    {
        var device = await _deviceRepository.GetByIdAsync(id);
        if (device == null) return false;

        await _deviceRepository.DeleteAsync(id);
        return true;
    }

    public async Task<IEnumerable<OfflineLogDto>> GetOfflineLogsAsync(int? deviceId = null)
    {
        IEnumerable<OfflineLog> logs;

        if (deviceId.HasValue)
        {
            logs = await _offlineLogRepository.GetByDeviceIdAsync(deviceId.Value);
        }
        else
        {
            logs = await _offlineLogRepository.GetAllAsync();
        }

        return logs.Select(l => new OfflineLogDto
        {
            Id = l.Id,
            DeviceId = l.DeviceId,
            DeviceName = l.DeviceName,
            IpAddress = l.IpAddress,
            Port = l.Port,
            OfflineAt = l.OfflineAt,
            ReconnectedAt = l.ReconnectedAt,
            Duration = l.Duration,
            Reason = l.Reason,
            ReconnectAttempts = l.ReconnectAttempts,
            IsResolved = l.IsResolved
        });
    }

    private DeviceDto MapToDto(Device device)
    {
        return new DeviceDto
        {
            Id = device.Id,
            Name = device.Name,
            IpAddress = device.IpAddress,
            Port = device.Port,
            SlaveId = device.SlaveId,
            Status = device.Status,
            Description = device.Description,
            ReconnectIntervalSeconds = device.ReconnectIntervalSeconds,
            MaxReconnectAttempts = device.MaxReconnectAttempts,
            IsActive = device.IsActive,
            LastConnectedAt = device.LastConnectedAt,
            LastDataReceivedAt = device.LastDataReceivedAt,
            RegisterMaps = device.RegisterMaps.Select(r => new RegisterMapDto
            {
                Id = r.Id,
                DeviceId = r.DeviceId,
                Name = r.Name,
                Description = r.Description,
                RegisterType = r.RegisterType,
                Address = r.Address,
                Length = r.Length,
                ScaleFactor = r.ScaleFactor,
                Offset = r.Offset,
                Unit = r.Unit,
                DataType = r.DataType,
                IsActive = r.IsActive,
                UpperThreshold = r.UpperThreshold,
                LowerThreshold = r.LowerThreshold,
                EnableAlarm = r.EnableAlarm,
                ControlCoilAddress = r.ControlCoilAddress,
                ControlCoilValue = r.ControlCoilValue,
                ControlDescription = r.ControlDescription
            }).ToList()
        };
    }
}
