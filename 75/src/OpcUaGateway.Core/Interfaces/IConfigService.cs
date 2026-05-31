using OpcUaGateway.Core.Models;
using OpcUaGateway.Core.DTOs;

namespace OpcUaGateway.Core.Interfaces;

public interface IConfigService
{
    Task<List<Device>> GetDevicesAsync();
    Task<Device?> GetDeviceAsync(string deviceId);
    Task<Device> AddDeviceAsync(DeviceConfigDto dto);
    Task<Device?> UpdateDeviceAsync(string deviceId, DeviceConfigDto dto);
    Task<bool> DeleteDeviceAsync(string deviceId);
    
    Task<List<DataPoint>> GetDataPointsAsync(string deviceId);
    Task<DataPoint?> GetDataPointAsync(string deviceId, string pointId);
    Task<DataPoint> AddDataPointAsync(string deviceId, DataPointDto dto);
    Task<DataPoint?> UpdateDataPointAsync(string deviceId, string pointId, DataPointDto dto);
    Task<bool> DeleteDataPointAsync(string deviceId, string pointId);
    
    Task<List<ThresholdAlert>> GetActiveAlertsAsync();
    Task<List<ThresholdAlert>> GetAlertHistoryAsync(int hours = 24);
    Task<bool> AcknowledgeAlertAsync(string alertId);
    
    Task SaveChangesAsync();
}