using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;
using IndustrialDataCollector.Core.Interfaces;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;

namespace IndustrialDataCollector.Infrastructure.Services;

public class DataCollectionService : BackgroundService
{
    private readonly IServiceProvider _serviceProvider;
    private readonly ILogger<DataCollectionService> _logger;
    private readonly Dictionary<int, ModbusService> _deviceConnections = new();
    private readonly Dictionary<int, OfflineLog> _activeOfflineLogs = new();
    private readonly Dictionary<int, SemaphoreSlim> _deviceLocks = new();
    private readonly object _lock = new();
    private const int CollectionIntervalMs = 1000;
    private const int DeviceTimeoutMs = 1000;

    public DataCollectionService(
        IServiceProvider serviceProvider,
        ILogger<DataCollectionService> logger)
    {
        _serviceProvider = serviceProvider;
        _logger = logger;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("Data Collection Service is starting");

        using var timer = new PeriodicTimer(TimeSpan.FromMilliseconds(CollectionIntervalMs));

        while (!stoppingToken.IsCancellationRequested && await timer.WaitForNextTickAsync(stoppingToken))
        {
            try
            {
                await CollectDataAsync(stoppingToken);
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error during data collection cycle");
            }
        }

        _logger.LogInformation("Data Collection Service is stopping");
        await CleanupConnectionsAsync();
    }

    private async Task CollectDataAsync(CancellationToken stoppingToken)
    {
        using var globalScope = _serviceProvider.CreateScope();
        var deviceRepository = globalScope.ServiceProvider.GetRequiredService<IDeviceRepository>();

        var activeDevices = (await deviceRepository.GetActiveDevicesAsync()).ToList();

        if (!activeDevices.Any()) return;

        var deviceTasks = activeDevices
            .Where(d => !stoppingToken.IsCancellationRequested)
            .Select(device => ProcessDeviceWithTimeoutAsync(device, stoppingToken));

        await Task.WhenAll(deviceTasks);
    }

    private async Task ProcessDeviceWithTimeoutAsync(Device device, CancellationToken stoppingToken)
    {
        using var deviceTimeoutCts = CancellationTokenSource.CreateLinkedTokenSource(stoppingToken);
        deviceTimeoutCts.CancelAfter(TimeSpan.FromMilliseconds(DeviceTimeoutMs));

        try
        {
            await ProcessDeviceAsync(device, deviceTimeoutCts.Token);
        }
        catch (OperationCanceledException) when (deviceTimeoutCts.IsCancellationRequested && !stoppingToken.IsCancellationRequested)
        {
            _logger.LogWarning("Device {DeviceName} ({IpAddress}:{Port}) collection timed out after {Timeout}ms, skipping this cycle",
                device.Name, device.IpAddress, device.Port, DeviceTimeoutMs);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error processing device {DeviceName}", device.Name);
            using var scope = _serviceProvider.CreateScope();
            var offlineLogRepository = scope.ServiceProvider.GetRequiredService<IOfflineLogRepository>();
            var deviceRepo = scope.ServiceProvider.GetRequiredService<IDeviceRepository>();
            await HandleDeviceOfflineAsync(device, deviceRepo, offlineLogRepository, ex.Message);
        }
    }

    private async Task ProcessDeviceAsync(
        Device device,
        CancellationToken cancellationToken)
    {
        SemaphoreSlim deviceLock;
        lock (_lock)
        {
            if (!_deviceLocks.TryGetValue(device.Id, out deviceLock))
            {
                deviceLock = new SemaphoreSlim(1, 1);
                _deviceLocks[device.Id] = deviceLock;
            }
        }

        await deviceLock.WaitAsync(cancellationToken);
        try
        {
            ModbusService? modbusService;
            lock (_lock)
            {
                if (!_deviceConnections.TryGetValue(device.Id, out modbusService))
                {
                    modbusService = new ModbusService(
                        _serviceProvider.GetRequiredService<ILogger<ModbusService>>());
                    _deviceConnections[device.Id] = modbusService;
                }
            }

            using var scope = _serviceProvider.CreateScope();
            var deviceRepository = scope.ServiceProvider.GetRequiredService<IDeviceRepository>();
            var dataStorage = scope.ServiceProvider.GetRequiredService<IDataStorage>();
            var offlineLogRepository = scope.ServiceProvider.GetRequiredService<IOfflineLogRepository>();

            var isConnected = await modbusService.IsConnectedAsync();
            cancellationToken.ThrowIfCancellationRequested();

            if (!isConnected)
            {
                if (device.Status == DeviceStatus.Online)
                {
                    await HandleDeviceOfflineAsync(device, deviceRepository, offlineLogRepository, "Connection lost");
                }
                else
                {
                    await TryReconnectAsync(device, modbusService, deviceRepository, offlineLogRepository);
                }
                return;
            }

            if (device.Status != DeviceStatus.Online)
            {
                await HandleDeviceReconnectedAsync(device, deviceRepository, offlineLogRepository);
            }

            var dataPoints = await ReadDeviceRegistersAsync(device, modbusService, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();

            if (dataPoints.Any())
            {
                await dataStorage.WriteDataAsync(dataPoints);
                await deviceRepository.UpdateStatusAsync(device.Id, DeviceStatus.Online, DateTime.UtcNow);
                device.LastDataReceivedAt = DateTime.UtcNow;

                await CheckAlarmsAsync(device, dataPoints, modbusService, scope);
            }
        }
        finally
        {
            deviceLock.Release();
        }
    }

    private async Task CheckAlarmsAsync(
        Device device,
        List<DataPoint> dataPoints,
        ModbusService modbusService,
        IServiceScope scope)
    {
        var alarmService = scope.ServiceProvider.GetRequiredService<AlarmService>();

        foreach (var dataPoint in dataPoints)
        {
            var registerMap = device.RegisterMaps
                .FirstOrDefault(r => r.Name == dataPoint.RegisterName);

            if (registerMap != null && registerMap.EnableAlarm)
            {
                await alarmService.CheckAndProcessAlarmsAsync(
                    device, registerMap, dataPoint.Value, modbusService);
            }
        }
    }

    private async Task<List<DataPoint>> ReadDeviceRegistersAsync(
        Device device,
        ModbusService modbusService,
        CancellationToken cancellationToken)
    {
        var dataPoints = new List<DataPoint>();
        var activeRegisters = device.RegisterMaps.Where(r => r.IsActive).ToList();

        if (!activeRegisters.Any()) return dataPoints;

        var groupedRegisters = activeRegisters
            .GroupBy(r => r.RegisterType)
            .ToDictionary(g => g.Key, g => g.ToList());

        foreach (var (registerType, registers) in groupedRegisters)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                var points = await ReadRegistersByTypeAsync(device, modbusService, registerType, registers, cancellationToken);
                dataPoints.AddRange(points);
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error reading {RegisterType} registers from device {DeviceName}",
                    registerType, device.Name);
            }
        }

        return dataPoints;
    }

    private async Task<List<DataPoint>> ReadRegistersByTypeAsync(
        Device device,
        ModbusService modbusService,
        RegisterType registerType,
        List<RegisterMap> registers,
        CancellationToken cancellationToken)
    {
        var dataPoints = new List<DataPoint>();

        foreach (var register in registers)
        {
            cancellationToken.ThrowIfCancellationRequested();

            try
            {
                ushort[]? rawValues = null;

                switch (registerType)
                {
                    case RegisterType.HoldingRegister:
                        rawValues = await modbusService.ReadHoldingRegistersAsync(register.Address, register.Length);
                        break;
                    case RegisterType.InputRegister:
                        rawValues = await modbusService.ReadInputRegistersAsync(register.Address, register.Length);
                        break;
                }

                if (rawValues != null && rawValues.Length > 0)
                {
                    var value = ConvertRawValue(rawValues, register.DataType, register.ScaleFactor, register.Offset);
                    var dataPoint = new DataPoint
                    {
                        Measurement = "sensor_data",
                        DeviceId = device.Id,
                        DeviceName = device.Name,
                        RegisterName = register.Name,
                        Value = value,
                        Unit = register.Unit,
                        Timestamp = DateTime.UtcNow
                    };
                    dataPoints.Add(dataPoint);
                }
            }
            catch (OperationCanceledException)
            {
                throw;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Error reading register {RegisterName} from device {DeviceName}",
                    register.Name, device.Name);
            }
        }

        return dataPoints;
    }

    private double ConvertRawValue(ushort[] rawValues, string dataType, double scaleFactor, double offset)
    {
        double value;

        switch (dataType.ToLower())
        {
            case "float":
                if (rawValues.Length >= 2)
                {
                    var bytes = new byte[4];
                    BitConverter.GetBytes(rawValues[0]).CopyTo(bytes, 2);
                    BitConverter.GetBytes(rawValues[1]).CopyTo(bytes, 0);
                    value = BitConverter.ToSingle(bytes, 0);
                }
                else
                {
                    value = rawValues[0] * scaleFactor + offset;
                }
                break;

            case "int32":
                if (rawValues.Length >= 2)
                {
                    value = (rawValues[0] << 16) | rawValues[1];
                }
                else
                {
                    value = rawValues[0];
                }
                break;

            case "uint16":
            default:
                value = rawValues[0];
                break;
        }

        return value * scaleFactor + offset;
    }

    private async Task TryReconnectAsync(
        Device device,
        ModbusService modbusService,
        IDeviceRepository deviceRepository,
        IOfflineLogRepository offlineLogRepository)
    {
        if (device.CurrentReconnectAttempts >= device.MaxReconnectAttempts)
        {
            if (device.Status != DeviceStatus.Error)
            {
                await deviceRepository.UpdateStatusAsync(device.Id, DeviceStatus.Error);
                _logger.LogWarning("Device {DeviceName} reached max reconnect attempts", device.Name);
            }
            return;
        }

        var now = DateTime.UtcNow;
        var lastDisconnected = device.LastDisconnectedAt ?? now;
        var interval = TimeSpan.FromSeconds(device.ReconnectIntervalSeconds);

        if (now - lastDisconnected < interval)
        {
            return;
        }

        device.CurrentReconnectAttempts++;
        await deviceRepository.UpdateStatusAsync(device.Id, DeviceStatus.Reconnecting, now);

        _logger.LogInformation("Attempting to reconnect to device {DeviceName} (Attempt {Attempt}/{MaxAttempts})",
            device.Name, device.CurrentReconnectAttempts, device.MaxReconnectAttempts);

        var connected = await modbusService.ConnectAsync(device.IpAddress, device.Port, device.SlaveId);

        if (connected)
        {
            await HandleDeviceReconnectedAsync(device, deviceRepository, offlineLogRepository);
        }
        else
        {
            device.LastDisconnectedAt = now;
        }
    }

    private async Task HandleDeviceOfflineAsync(
        Device device,
        IDeviceRepository deviceRepository,
        IOfflineLogRepository offlineLogRepository,
        string reason)
    {
        await deviceRepository.UpdateStatusAsync(device.Id, DeviceStatus.Offline, DateTime.UtcNow);

        lock (_lock)
        {
            if (_deviceConnections.ContainsKey(device.Id))
            {
                _deviceConnections[device.Id].Dispose();
                _deviceConnections.Remove(device.Id);
            }
        }

        if (!_activeOfflineLogs.ContainsKey(device.Id))
        {
            var offlineLog = new OfflineLog
            {
                DeviceId = device.Id,
                DeviceName = device.Name,
                IpAddress = device.IpAddress,
                Port = device.Port,
                OfflineAt = DateTime.UtcNow,
                Reason = reason,
                ReconnectAttempts = device.CurrentReconnectAttempts,
                IsResolved = false
            };

            await offlineLogRepository.AddAsync(offlineLog);
            _activeOfflineLogs[device.Id] = offlineLog;

            _logger.LogWarning("Device {DeviceName} went offline: {Reason}", device.Name, reason);
        }
    }

    private async Task HandleDeviceReconnectedAsync(
        Device device,
        IDeviceRepository deviceRepository,
        IOfflineLogRepository offlineLogRepository)
    {
        await deviceRepository.UpdateStatusAsync(device.Id, DeviceStatus.Online, DateTime.UtcNow);

        if (_activeOfflineLogs.TryGetValue(device.Id, out var offlineLog))
        {
            offlineLog.ReconnectedAt = DateTime.UtcNow;
            offlineLog.IsResolved = true;
            offlineLog.ReconnectAttempts = device.CurrentReconnectAttempts;
            await offlineLogRepository.UpdateAsync(offlineLog);
            _activeOfflineLogs.Remove(device.Id);

            _logger.LogInformation("Device {DeviceName} reconnected successfully after {Duration}",
                device.Name, offlineLog.Duration);
        }
    }

    private async Task CleanupConnectionsAsync()
    {
        lock (_lock)
        {
            foreach (var connection in _deviceConnections.Values)
            {
                try
                {
                    connection.Dispose();
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error cleaning up Modbus connection");
                }
            }
            _deviceConnections.Clear();

            foreach (var deviceLock in _deviceLocks.Values)
            {
                try
                {
                    deviceLock.Dispose();
                }
                catch { }
            }
            _deviceLocks.Clear();
        }

        await Task.CompletedTask;
    }
}
