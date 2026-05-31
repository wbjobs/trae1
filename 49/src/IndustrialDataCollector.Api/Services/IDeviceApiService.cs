using IndustrialDataCollector.Core.DTOs;

namespace IndustrialDataCollector.Api.Services;

public interface IDeviceApiService
{
    Task<IEnumerable<DeviceDto>> GetDevicesAsync();
    Task<DeviceDto?> GetDeviceByIdAsync(int id);
    Task<DeviceDto> AddDeviceAsync(DeviceCreateDto dto);
    Task<DeviceDto?> UpdateDeviceAsync(int id, DeviceUpdateDto dto);
    Task<bool> DeleteDeviceAsync(int id);
    Task<IEnumerable<OfflineLogDto>> GetOfflineLogsAsync(int? deviceId = null);
    Task<IEnumerable<DataPointDto>> GetDeviceDataAsync(int id, DateTime? startTime, DateTime? endTime);
    Task<IEnumerable<DataPointDto>> GetLatestDeviceDataAsync(int id, int limit = 100);
    Task<IEnumerable<DataPointDto>> QueryDataAsync(DataQueryDto dto);
}
