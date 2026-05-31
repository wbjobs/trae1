using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using OpcUaGateway.Core.DTOs;
using Microsoft.Extensions.Logging;
using System.Text.Json;

namespace OpcUaGateway.Application.Services;

public class ConfigService : IConfigService
{
    private readonly ILogger<ConfigService> _logger;
    private readonly IThresholdAlertService _alertService;
    private List<Device> _devices = new();
    private readonly object _lock = new();
    private readonly string _configFilePath = "gateway_config.json";

    public ConfigService(ILogger<ConfigService> logger, IThresholdAlertService alertService)
    {
        _logger = logger;
        _alertService = alertService;
        LoadDefaultConfig();
    }

    private void LoadDefaultConfig()
    {
        if (File.Exists(_configFilePath))
        {
            try
            {
                var json = File.ReadAllText(_configFilePath);
                var config = JsonSerializer.Deserialize<GatewayConfigData>(json);
                if (config?.Devices != null)
                {
                    _devices = config.Devices;
                    _logger.LogInformation("Loaded {Count} devices from config file", _devices.Count);
                    return;
                }
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Failed to load config file, using defaults");
            }
        }

        _devices = CreateDefaultDevices();
        _logger.LogInformation("Created default configuration with {Count} devices", _devices.Count);
    }

    private List<Device> CreateDefaultDevices()
    {
        var devices = new List<Device>();
        var pointTypes = new[]
        {
            new { Name = "温度", Unit = "℃", Upper = 120.0, Lower = -10.0 },
            new { Name = "压力", Unit = "MPa", Upper = 10.0, Lower = 0.0 },
            new { Name = "转速", Unit = "RPM", Upper = 3600.0, Lower = 0.0 },
            new { Name = "振动", Unit = "mm/s", Upper = 7.1, Lower = 0.0 },
            new { Name = "电流", Unit = "A", Upper = 50.0, Lower = 0.0 }
        };

        for (int i = 1; i <= 10; i++)
        {
            var device = new Device
            {
                DeviceId = $"PLC-{i:00}",
                DeviceName = $"车间PLC-{i:00}",
                EndpointUrl = $"opc.tcp://192.168.1.{100 + i}:4840",
                Username = "opcua",
                Password = "opcua",
                UseSecurity = false,
                IsEnabled = true,
                PollIntervalMs = 100
            };

            for (int j = 1; j <= 10; j++)
            {
                var pointType = pointTypes[(j - 1) % pointTypes.Length];
                var pointIndex = (j - 1) / pointTypes.Length + 1;

                var dataPoint = new DataPoint
                {
                    PointId = $"{device.DeviceId}_P{j:00}",
                    DeviceId = device.DeviceId,
                    NodeId = $"ns=2;s=Channel1.Device{j}.Tag{j}",
                    PointName = $"{pointType.Name}{pointIndex}",
                    DataType = "Double",
                    Unit = pointType.Unit,
                    Description = $"{device.DeviceName} {pointType.Name}采集点{pointIndex}",
                    IsEnabled = true,
                    UpperThreshold = pointType.Upper,
                    LowerThreshold = pointType.Lower
                };

                device.DataPoints.Add(dataPoint);
            }

            devices.Add(device);
        }

        return devices;
    }

    public Task<List<Device>> GetDevicesAsync()
    {
        lock (_lock)
        {
            return Task.FromResult(_devices.Select(d => new Device
            {
                DeviceId = d.DeviceId,
                DeviceName = d.DeviceName,
                EndpointUrl = d.EndpointUrl,
                Username = d.Username,
                Password = d.Password,
                UseSecurity = d.UseSecurity,
                IsEnabled = d.IsEnabled,
                PollIntervalMs = d.PollIntervalMs,
                DataPoints = d.DataPoints.Select(p => new DataPoint
                {
                    PointId = p.PointId,
                    DeviceId = p.DeviceId,
                    NodeId = p.NodeId,
                    PointName = p.PointName,
                    DataType = p.DataType,
                    Unit = p.Unit,
                    Description = p.Description,
                    IsEnabled = p.IsEnabled,
                    UpperThreshold = p.UpperThreshold,
                    LowerThreshold = p.LowerThreshold,
                    RateOfChangeThreshold = p.RateOfChangeThreshold
                }).ToList()
            }).ToList());
        }
    }

    public Task<Device?> GetDeviceAsync(string deviceId)
    {
        lock (_lock)
        {
            return Task.FromResult(_devices.FirstOrDefault(d => d.DeviceId == deviceId));
        }
    }

    public async Task<Device> AddDeviceAsync(DeviceConfigDto dto)
    {
        lock (_lock)
        {
            var deviceId = $"PLC-{_devices.Count + 1:00}";
            var device = new Device
            {
                DeviceId = deviceId,
                DeviceName = dto.DeviceName,
                EndpointUrl = dto.EndpointUrl,
                Username = dto.Username,
                Password = dto.Password,
                UseSecurity = dto.UseSecurity,
                IsEnabled = dto.IsEnabled,
                PollIntervalMs = dto.PollIntervalMs,
                CreatedAt = DateTime.UtcNow,
                UpdatedAt = DateTime.UtcNow
            };

            _devices.Add(device);
            SaveConfig();

            _logger.LogInformation("Added device {DeviceId} ({DeviceName})", deviceId, dto.DeviceName);
            return device;
        }
    }

    public async Task<Device?> UpdateDeviceAsync(string deviceId, DeviceConfigDto dto)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            if (device == null) return null;

            device.DeviceName = dto.DeviceName;
            device.EndpointUrl = dto.EndpointUrl;
            device.Username = dto.Username;
            device.Password = dto.Password;
            device.UseSecurity = dto.UseSecurity;
            device.IsEnabled = dto.IsEnabled;
            device.PollIntervalMs = dto.PollIntervalMs;
            device.UpdatedAt = DateTime.UtcNow;

            SaveConfig();

            _logger.LogInformation("Updated device {DeviceId}", deviceId);
            return device;
        }
    }

    public Task<bool> DeleteDeviceAsync(string deviceId)
    {
        lock (_lock)
        {
            var removed = _devices.RemoveAll(d => d.DeviceId == deviceId);
            if (removed > 0)
            {
                SaveConfig();
                _logger.LogInformation("Deleted device {DeviceId}", deviceId);
            }
            return Task.FromResult(removed > 0);
        }
    }

    public Task<List<DataPoint>> GetDataPointsAsync(string deviceId)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            return Task.FromResult(device?.DataPoints ?? new List<DataPoint>());
        }
    }

    public Task<DataPoint?> GetDataPointAsync(string deviceId, string pointId)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            var point = device?.DataPoints.FirstOrDefault(p => p.PointId == pointId);
            return Task.FromResult(point);
        }
    }

    public async Task<DataPoint> AddDataPointAsync(string deviceId, DataPointDto dto)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            if (device == null)
                throw new InvalidOperationException($"Device {deviceId} not found");

            var pointId = $"{deviceId}_P{device.DataPoints.Count + 1:00}";
            var dataPoint = new DataPoint
            {
                PointId = pointId,
                DeviceId = deviceId,
                NodeId = dto.NodeId,
                PointName = dto.PointName,
                DataType = dto.DataType,
                Unit = dto.Unit,
                Description = dto.Description,
                IsEnabled = dto.IsEnabled,
                UpperThreshold = dto.UpperThreshold,
                LowerThreshold = dto.LowerThreshold,
                RateOfChangeThreshold = dto.RateOfChangeThreshold
            };

            device.DataPoints.Add(dataPoint);
            SaveConfig();

            _logger.LogInformation("Added data point {PointId} to device {DeviceId}", pointId, deviceId);
            return dataPoint;
        }
    }

    public async Task<DataPoint?> UpdateDataPointAsync(string deviceId, string pointId, DataPointDto dto)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            var point = device?.DataPoints.FirstOrDefault(p => p.PointId == pointId);
            if (point == null) return null;

            point.NodeId = dto.NodeId;
            point.PointName = dto.PointName;
            point.DataType = dto.DataType;
            point.Unit = dto.Unit;
            point.Description = dto.Description;
            point.IsEnabled = dto.IsEnabled;
            point.UpperThreshold = dto.UpperThreshold;
            point.LowerThreshold = dto.LowerThreshold;
            point.RateOfChangeThreshold = dto.RateOfChangeThreshold;

            SaveConfig();

            _logger.LogInformation("Updated data point {PointId}", pointId);
            return point;
        }
    }

    public Task<bool> DeleteDataPointAsync(string deviceId, string pointId)
    {
        lock (_lock)
        {
            var device = _devices.FirstOrDefault(d => d.DeviceId == deviceId);
            if (device == null) return Task.FromResult(false);

            var removed = device.DataPoints.RemoveAll(p => p.PointId == pointId);
            if (removed > 0)
            {
                SaveConfig();
                _logger.LogInformation("Deleted data point {PointId}", pointId);
            }

            return Task.FromResult(removed > 0);
        }
    }

    public Task<List<ThresholdAlert>> GetActiveAlertsAsync()
    {
        return Task.FromResult(_alertService.GetActiveAlerts());
    }

    public Task<List<ThresholdAlert>> GetAlertHistoryAsync(int hours = 24)
    {
        return Task.FromResult(_alertService.GetAlertHistory(hours));
    }

    public async Task<bool> AcknowledgeAlertAsync(string alertId)
    {
        await _alertService.AcknowledgeAlertAsync(alertId);
        return true;
    }

    public Task SaveChangesAsync()
    {
        SaveConfig();
        return Task.CompletedTask;
    }

    private void SaveConfig()
    {
        try
        {
            var config = new GatewayConfigData { Devices = _devices };
            var json = JsonSerializer.Serialize(config, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(_configFilePath, json);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to save config file");
        }
    }

    private class GatewayConfigData
    {
        public List<Device> Devices { get; set; } = new();
    }
}