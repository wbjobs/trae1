using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Infrastructure.Services;

namespace IndustrialDataCollector.Api.Services;

public class DeviceApiService : IDeviceApiService
{
    private readonly DeviceService _deviceService;
    private readonly IDataStorage _dataStorage;

    public DeviceApiService(
        DeviceService deviceService,
        IDataStorage dataStorage)
    {
        _deviceService = deviceService;
        _dataStorage = dataStorage;
    }

    public async Task<IEnumerable<DeviceDto>> GetDevicesAsync()
    {
        return await _deviceService.GetAllDevicesAsync();
    }

    public async Task<DeviceDto?> GetDeviceByIdAsync(int id)
    {
        return await _deviceService.GetDeviceByIdAsync(id);
    }

    public async Task<DeviceDto> AddDeviceAsync(DeviceCreateDto dto)
    {
        return await _deviceService.AddDeviceAsync(dto);
    }

    public async Task<DeviceDto?> UpdateDeviceAsync(int id, DeviceUpdateDto dto)
    {
        return await _deviceService.UpdateDeviceAsync(id, dto);
    }

    public async Task<bool> DeleteDeviceAsync(int id)
    {
        return await _deviceService.DeleteDeviceAsync(id);
    }

    public async Task<IEnumerable<OfflineLogDto>> GetOfflineLogsAsync(int? deviceId = null)
    {
        return await _deviceService.GetOfflineLogsAsync(deviceId);
    }

    public async Task<IEnumerable<DataPointDto>> GetDeviceDataAsync(int id, DateTime? startTime, DateTime? endTime)
    {
        var device = await _deviceService.GetDeviceByIdAsync(id);
        if (device == null) return Enumerable.Empty<DataPointDto>();

        var start = startTime ?? DateTime.UtcNow.AddHours(-1);
        var end = endTime ?? DateTime.UtcNow;

        var dataPoints = await _dataStorage.QueryDataAsync("sensor_data", start, end, device.Name);

        return dataPoints.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });
    }

    public async Task<IEnumerable<DataPointDto>> GetLatestDeviceDataAsync(int id, int limit = 100)
    {
        var device = await _deviceService.GetDeviceByIdAsync(id);
        if (device == null) return Enumerable.Empty<DataPointDto>();

        var dataPoints = await _dataStorage.GetLatestDataAsync("sensor_data", device.Name, limit);

        return dataPoints.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });
    }

    public async Task<IEnumerable<DataPointDto>> QueryDataAsync(DataQueryDto dto)
    {
        var dataPoints = await _dataStorage.QueryDataAsync(
            dto.Measurement,
            dto.StartTime,
            dto.EndTime);

        var filteredData = dataPoints.AsEnumerable();

        if (dto.DeviceIds != null && dto.DeviceIds.Any())
        {
            filteredData = filteredData.Where(dp => dto.DeviceIds.Contains(dp.DeviceId));
        }

        if (dto.RegisterNames != null && dto.RegisterNames.Any())
        {
            filteredData = filteredData.Where(dp => dto.RegisterNames.Contains(dp.RegisterName));
        }

        return filteredData.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });
    }
}
