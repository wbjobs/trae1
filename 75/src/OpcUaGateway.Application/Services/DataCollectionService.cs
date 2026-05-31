using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using OpcUaGateway.Core.Configuration;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.DependencyInjection;

namespace OpcUaGateway.Application.Services;

public class DataCollectionService : IDataCollectionService
{
    private readonly ILogger<DataCollectionService> _logger;
    private readonly IConfigService _configService;
    private readonly IMqttClientService _mqttClient;
    private readonly IOfflineRepository _offlineRepository;
    private readonly IServiceProvider _serviceProvider;
    private readonly ICertificateManager _certificateManager;
    private readonly IEmailNotificationService? _emailService;
    private readonly IRuleEngine _ruleEngine;
    private readonly IRuleService _ruleService;
    private readonly GatewayConfig _config;

    private readonly Dictionary<string, IOpcUaClient> _opcClients = new();
    private readonly Dictionary<string, DeviceStatus> _deviceStatuses = new();
    private readonly Dictionary<string, List<DataPoint>> _deviceDataPoints = new();
    private readonly Dictionary<string, DateTime> _lastCollectTimes = new();
    private readonly Dictionary<string, Dictionary<string, double>> _lastValues = new();
    private readonly Dictionary<string, string> _deviceCertThumbprints = new();
    private readonly Dictionary<string, DateTime> _lastReconnectAttempts = new();
    
    private CancellationTokenSource? _cancellationTokenSource;
    private Task? _collectionTask;
    private Task? _offlineFlushTask;
    private Task? _healthCheckTask;
    private readonly object _lock = new();
    private DateTime _lastConfigRefresh = DateTime.MinValue;

    public DataCollectionService(
        ILogger<DataCollectionService> logger,
        IConfigService configService,
        IMqttClientService mqttClient,
        IOfflineRepository offlineRepository,
        IServiceProvider serviceProvider,
        ICertificateManager certificateManager,
        IRuleEngine ruleEngine,
        IRuleService ruleService,
        IOptions<GatewayConfig> config,
        IEmailNotificationService? emailService = null)
    {
        _logger = logger;
        _configService = configService;
        _mqttClient = mqttClient;
        _offlineRepository = offlineRepository;
        _serviceProvider = serviceProvider;
        _certificateManager = certificateManager;
        _ruleEngine = ruleEngine;
        _ruleService = ruleService;
        _emailService = emailService;
        _config = config.Value;

        _ruleEngine.RuleAutoDisabled += RuleEngineService_OnRuleAutoDisabled;
    }

    public event EventHandler<DataCollectedEventArgs>? DataCollected;
    public event EventHandler<DeviceStatusChangedEventArgs>? DeviceStatusChanged;

    public async Task StartAsync(CancellationToken cancellationToken)
    {
        _cancellationTokenSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var token = _cancellationTokenSource.Token;

        _logger.LogInformation("Starting data collection service...");

        await _certificateManager.InitializeAsync();
        await _ruleService.InitializeAsync();
        await _ruleEngine.InitializeAsync();

        var devices = await _configService.GetDevicesAsync();
        var enabledDevices = devices.Where(d => d.IsEnabled).ToList();

        _logger.LogInformation("Found {Count} enabled devices to monitor", enabledDevices.Count);

        foreach (var device in enabledDevices)
        {
            await InitializeDeviceAsync(device);
        }

        _collectionTask = Task.Run(() => CollectionLoopAsync(token), token);
        _offlineFlushTask = Task.Run(() => OfflineFlushLoopAsync(token), token);
        _healthCheckTask = Task.Run(() => HealthCheckLoopAsync(token), token);

        _logger.LogInformation("Data collection service started successfully");
    }

    public async Task StopAsync()
    {
        _logger.LogInformation("Stopping data collection service...");

        _cancellationTokenSource?.Cancel();

        if (_collectionTask != null)
        {
            await Task.WhenAny(_collectionTask, Task.Delay(TimeSpan.FromSeconds(10)));
        }

        if (_offlineFlushTask != null)
        {
            await Task.WhenAny(_offlineFlushTask, Task.Delay(TimeSpan.FromSeconds(10)));
        }

        if (_healthCheckTask != null)
        {
            await Task.WhenAny(_healthCheckTask, Task.Delay(TimeSpan.FromSeconds(10)));
        }

        foreach (var client in _opcClients.Values)
        {
            await client.DisconnectAsync();
        }

        _opcClients.Clear();
        _logger.LogInformation("Data collection service stopped");
    }

    public Task<IEnumerable<DeviceStatus>> GetDeviceStatusesAsync()
    {
        lock (_lock)
        {
            return Task.FromResult(_deviceStatuses.Values.AsEnumerable());
        }
    }

    public Task<DeviceStatus?> GetDeviceStatusAsync(string deviceId)
    {
        lock (_lock)
        {
            return Task.FromResult(_deviceStatuses.GetValueOrDefault(deviceId));
        }
    }

    private async Task InitializeDeviceAsync(Device device)
    {
        _logger.LogInformation("Initializing device {DeviceName} ({DeviceId})", device.DeviceName, device.DeviceId);

        var opcClient = _serviceProvider.GetRequiredService<IOpcUaClient>();
        _opcClients[device.DeviceId] = opcClient;

        var status = new DeviceStatus
        {
            DeviceId = device.DeviceId,
            DeviceName = device.DeviceName,
            Status = DeviceStatusType.Offline,
            LastUpdateTime = DateTime.UtcNow
        };

        lock (_lock)
        {
            _deviceStatuses[device.DeviceId] = status;
        }

        var dataPoints = await _configService.GetDataPointsAsync(device.DeviceId);
        var enabledPoints = dataPoints.Where(p => p.IsEnabled).ToList();
        _deviceDataPoints[device.DeviceId] = enabledPoints;

        _logger.LogInformation("Device {DeviceName} has {Count} enabled data points", device.DeviceName, enabledPoints.Count);

        var isConnected = await opcClient.ConnectAsync(
            device.EndpointUrl,
            device.Username,
            device.Password,
            device.UseSecurity);

        if (isConnected)
        {
            UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Online, "Connected");
            CaptureCurrentCertificate(device);
        }
        else
        {
            UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Error, "Failed to connect");
        }

        opcClient.ConnectionStateChanged += async (s, e) =>
        {
            var newStatus = e.IsConnected ? DeviceStatusType.Online : DeviceStatusType.Offline;
            UpdateDeviceStatus(device.DeviceId, newStatus, e.Message);

            if (e.IsConnected)
            {
                CaptureCurrentCertificate(device);
            }
        };

        opcClient.ServerCertificateChanged += async (s, e) =>
        {
            _logger.LogInformation("Certificate change detected for device {DeviceId}: {Endpoint}",
                device.DeviceId, e.EndpointUrl);

            if (_config.Certificate.AutoRenegotiateOnChange)
            {
                _logger.LogInformation("Auto-renegotiating security for device {DeviceId}", device.DeviceId);
                await HandleCertificateChangeAsync(device, opcClient, e);
            }
        };

        _lastValues[device.DeviceId] = new Dictionary<string, double>();
        _lastCollectTimes[device.DeviceId] = DateTime.UtcNow;
    }

    private void CaptureCurrentCertificate(Device device)
    {
        if (_opcClients.TryGetValue(device.DeviceId, out var client))
        {
            var cert = client.CurrentServerCertificate;
            if (cert != null)
            {
                var oldThumbprint = _deviceCertThumbprints.GetValueOrDefault(device.DeviceId);
                if (!string.IsNullOrEmpty(oldThumbprint) && oldThumbprint != cert.Thumbprint)
                {
                    _logger.LogInformation("Certificate change detected for {DeviceId}: {OldThumbprint} -> {NewThumbprint}",
                        device.DeviceId, oldThumbprint, cert.Thumbprint);
                }

                _deviceCertThumbprints[device.DeviceId] = cert.Thumbprint;

                if (cert.DaysUntilExpiry <= _config.Certificate.ExpiryWarningDays)
                {
                    _logger.LogWarning("Certificate for {DeviceId} expires in {Days} days",
                        device.DeviceId, cert.DaysUntilExpiry);

                    if (_emailService != null)
                    {
                        _ = _emailService.SendCertificateExpiryWarningAsync(
                            device.DeviceId, device.DeviceName, cert, cert.DaysUntilExpiry);
                    }
                }
            }
        }
    }

    private async Task HandleCertificateChangeAsync(Device device, IOpcUaClient opcClient, ServerCertificateChangedEventArgs e)
    {
        try
        {
            _deviceCertThumbprints[device.DeviceId] = e.NewCertificate.Thumbprint;

            var isWhitelisted = await _certificateManager.IsWhitelistedAsync(
                e.NewCertificate.Thumbprint, device.DeviceId);

            if (_config.Certificate.AcceptanceStrategy == CertificateAcceptanceStrategy.AutoTrustAll ||
                (_config.Certificate.AcceptanceStrategy == CertificateAcceptanceStrategy.WhitelistOnly && isWhitelisted))
            {
                var reconnectSuccess = await opcClient.ReconnectAsync();

                if (reconnectSuccess)
                {
                    _logger.LogInformation("Successfully reconnected to {DeviceId} after certificate change", device.DeviceId);
                    UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Online,
                        "Reconnected after certificate renewal");
                }
                else
                {
                    UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Error,
                        "Failed to reconnect after certificate change");
                }
            }
            else
            {
                UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Error,
                    "Certificate changed but not accepted by policy");
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error handling certificate change for {DeviceId}", device.DeviceId);
        }
    }

    private async Task CollectionLoopAsync(CancellationToken cancellationToken)
    {
        _logger.LogInformation("Collection loop started with {IntervalMs}ms interval", _config.PollIntervalMs);

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if ((DateTime.UtcNow - _lastConfigRefresh).TotalMinutes >= 5)
                {
                    var devices = await _configService.GetDevicesAsync();
                    var enabledDevices = devices.Where(d => d.IsEnabled).ToList();

                    foreach (var device in enabledDevices)
                    {
                        if (!_opcClients.ContainsKey(device.DeviceId))
                        {
                            _logger.LogInformation("New device detected, initializing {DeviceId}", device.DeviceId);
                            await InitializeDeviceAsync(device);
                        }
                    }

                    _lastConfigRefresh = DateTime.UtcNow;
                }

                var currentDevices = await _configService.GetDevicesAsync();
                var enabledCurrentDevices = currentDevices.Where(d => d.IsEnabled).ToList();

                foreach (var device in enabledCurrentDevices)
                {
                    if (cancellationToken.IsCancellationRequested) break;

                    try
                    {
                        await CollectFromDeviceAsync(device, cancellationToken);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Error collecting data from device {DeviceId}", device.DeviceId);
                        UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Error, ex.Message);
                    }
                }
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error in collection loop");
            }

            await Task.Delay(_config.PollIntervalMs, cancellationToken);
        }
    }

    private async Task CollectFromDeviceAsync(Device device, CancellationToken cancellationToken)
    {
        if (!_opcClients.TryGetValue(device.DeviceId, out var opcClient))
        {
            _logger.LogWarning("No OPC UA client found for device {DeviceId}", device.DeviceId);
            return;
        }

        var isConnected = await opcClient.IsConnectedAsync();
        if (!isConnected)
        {
            UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Offline, "Not connected");
            return;
        }

        var dataPoints = _deviceDataPoints.GetValueOrDefault(device.DeviceId) ?? new List<DataPoint>();
        if (dataPoints.Count == 0) return;

        var nodeIds = dataPoints.Select(p => p.NodeId).ToList();
        var readResults = await opcClient.ReadNodesAsync(nodeIds);

        var collectResults = new List<DataPointReadResult>();
        var mqttMessages = new List<MqttMessage>();
        var offlineMessages = new List<OfflineMessage>();

        foreach (var dataPoint in dataPoints)
        {
            if (readResults.TryGetValue(dataPoint.NodeId, out var result))
            {
                var quality = result.Quality;
                if (quality >= 192) 
                {
                    var lastValues = _lastValues.GetValueOrDefault(device.DeviceId) ?? new Dictionary<string, double>();

                    lastValues[dataPoint.PointId] = result.Value;
                    _lastValues[device.DeviceId] = lastValues;

                    var collectResult = new DataPointReadResult
                    {
                        PointId = dataPoint.PointId,
                        NodeId = dataPoint.NodeId,
                        PointName = dataPoint.PointName,
                        Value = result.Value,
                        Unit = dataPoint.Unit,
                        Quality = quality,
                        Timestamp = result.Timestamp
                    };
                    collectResults.Add(collectResult);

                    var mqttMessage = new MqttMessage
                    {
                        DeviceId = device.DeviceId,
                        PointId = dataPoint.PointId,
                        PointName = dataPoint.PointName,
                        Value = result.Value,
                        Unit = dataPoint.Unit,
                        Timestamp = result.Timestamp,
                        Quality = quality,
                        DataType = dataPoint.DataType
                    };
                    mqttMessages.Add(mqttMessage);

                    var offlineMessage = new OfflineMessage
                    {
                        DeviceId = device.DeviceId,
                        PointId = dataPoint.PointId,
                        PointName = dataPoint.PointName,
                        Value = result.Value,
                        Unit = dataPoint.Unit,
                        Timestamp = result.Timestamp,
                        Quality = quality,
                        DataType = dataPoint.DataType,
                        CreatedAt = DateTime.UtcNow
                    };
                    offlineMessages.Add(offlineMessage);
                }
            }
        }

        if (collectResults.Count > 0)
        {
            OnDataCollected(device, collectResults);
            UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Online, null);
            IncrementSuccessfulCollect(device.DeviceId);

            var virtualPoints = await ApplyCalculationRulesAsync(device, collectResults);
            mqttMessages.AddRange(virtualPoints);

            var mqttConnected = await _mqttClient.IsConnectedAsync();
            if (mqttConnected)
            {
                var topic = $"{device.DeviceId}";
                await _mqttClient.PublishBatchAsync(topic, mqttMessages);
            }
            else
            {
                await _offlineRepository.SaveMessagesBatchAsync(offlineMessages);
            }
        }
        else
        {
            IncrementFailedCollect(device.DeviceId);
        }

        _lastCollectTimes[device.DeviceId] = DateTime.UtcNow;
    }

    private async Task OfflineFlushLoopAsync(CancellationToken cancellationToken)
    {
        _logger.LogInformation("Offline flush loop started with {IntervalMs}ms interval", _config.OfflineFlushIntervalMs);

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var pendingCount = await _offlineRepository.GetPendingCountAsync();
                if (pendingCount > 0)
                {
                    _logger.LogInformation("Found {Count} pending offline messages", pendingCount);

                    var mqttConnected = await _mqttClient.IsConnectedAsync();
                    if (mqttConnected)
                    {
                        var pendingMessages = await _offlineRepository.GetPendingMessagesAsync(_config.BatchSize);
                        var sentIds = new List<long>();

                        foreach (var message in pendingMessages)
                        {
                            var mqttMessage = message.ToMqttMessage();
                            var topic = $"{message.DeviceId}";
                            var success = await _mqttClient.PublishAsync(topic, mqttMessage);

                            if (success)
                            {
                                sentIds.Add(message.Id);
                            }
                            else
                            {
                                _logger.LogWarning("Failed to republish offline message {Id}", message.Id);
                            }
                        }

                        if (sentIds.Count > 0)
                        {
                            await _offlineRepository.MarkBatchAsSentAsync(sentIds);
                            _logger.LogInformation("Republished {Count} offline messages", sentIds.Count);
                        }
                    }
                }

                await _offlineRepository.CleanupOldMessagesAsync(30);
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error in offline flush loop");
            }

            await Task.Delay(_config.OfflineFlushIntervalMs, cancellationToken);
        }
    }

    private async Task HealthCheckLoopAsync(CancellationToken cancellationToken)
    {
        _logger.LogInformation("Health check loop started (interval: 30s)");

        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var devices = await _configService.GetDevicesAsync();
                var enabledDevices = devices.Where(d => d.IsEnabled).ToList();

                foreach (var device in enabledDevices)
                {
                    if (cancellationToken.IsCancellationRequested) break;

                    try
                    {
                        await CheckDeviceHealthAsync(device);
                    }
                    catch (Exception ex)
                    {
                        _logger.LogError(ex, "Error in health check for {DeviceId}", device.DeviceId);
                    }
                }

                await _certificateManager.CleanupOldAuditLogsAsync();
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Error in health check loop");
            }

            await Task.Delay(TimeSpan.FromSeconds(30), cancellationToken);
        }
    }

    private async Task CheckDeviceHealthAsync(Device device)
    {
        if (!_opcClients.TryGetValue(device.DeviceId, out var opcClient)) return;

        var isConnected = await opcClient.IsConnectedAsync();
        if (!isConnected)
        {
            var lastAttempt = _lastReconnectAttempts.GetValueOrDefault(device.DeviceId);
            if ((DateTime.UtcNow - lastAttempt).TotalSeconds >= 30)
            {
                _logger.LogInformation("Attempting reconnection for {DeviceId}", device.DeviceId);
                _lastReconnectAttempts[device.DeviceId] = DateTime.UtcNow;

                var reconnectSuccess = await opcClient.ReconnectAsync();

                if (reconnectSuccess)
                {
                    UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Online, "Reconnected");
                    CaptureCurrentCertificate(device);
                }
                else
                {
                    UpdateDeviceStatus(device.DeviceId, DeviceStatusType.Offline, "Reconnection failed");
                }
            }
        }
    }

    private void OnDataCollected(Device device, List<DataPointReadResult> results)
    {
        DataCollected?.Invoke(this, new DataCollectedEventArgs
        {
            DeviceId = device.DeviceId,
            DeviceName = device.DeviceName,
            Results = results,
            Timestamp = DateTime.UtcNow
        });
    }

    private void UpdateDeviceStatus(string deviceId, DeviceStatusType status, string? message)
    {
        lock (_lock)
        {
            if (_deviceStatuses.TryGetValue(deviceId, out var currentStatus))
            {
                var oldStatus = currentStatus.Status;
                currentStatus.Status = status;
                currentStatus.LastUpdateTime = DateTime.UtcNow;
                currentStatus.IsConnected = status == DeviceStatusType.Online;
                currentStatus.ErrorMessage = message;

                if (oldStatus != status)
                {
                    DeviceStatusChanged?.Invoke(this, new DeviceStatusChangedEventArgs
                    {
                        DeviceId = deviceId,
                        NewStatus = status,
                        Message = message,
                        Timestamp = DateTime.UtcNow
                    });
                }
            }
        }
    }

    private void IncrementSuccessfulCollect(string deviceId)
    {
        lock (_lock)
        {
            if (_deviceStatuses.TryGetValue(deviceId, out var status))
            {
                status.SuccessfulCollectCount++;
                status.LastSuccessfulCollectTime = DateTime.UtcNow;
            }
        }
    }

    private void IncrementFailedCollect(string deviceId)
    {
        lock (_lock)
        {
            if (_deviceStatuses.TryGetValue(deviceId, out var status))
            {
                status.FailedCollectCount++;
            }
        }
    }

    private async Task<List<MqttMessage>> ApplyCalculationRulesAsync(
        Device device,
        List<DataPointReadResult> collectedData)
    {
        var virtualMessages = new List<MqttMessage>();

        if (!_config.RuleEngine.EnableRuleEngine)
            return virtualMessages;

        var rules = await _ruleService.GetRulesAsync(device.DeviceId);
        var activeRules = rules.Where(r =>
            r.IsEnabled &&
            r.Status != RuleExecutionStatus.AutoDisabled_Performance &&
            r.Status != RuleExecutionStatus.AutoDisabled_Error).ToList();

        if (activeRules.Count == 0)
            return virtualMessages;

        var context = collectedData.ToDictionary(
            r => r.PointId,
            r => r.Value);

        foreach (var rule in activeRules)
        {
            try
            {
                if (!context.TryGetValue(rule.SourcePointId, out var sourceValue))
                {
                    _logger.LogDebug(
                        "Rule {RuleName}: source point {SourcePointId} not found in collected data",
                        rule.RuleName, rule.SourcePointId);
                    continue;
                }

                var result = await _ruleEngine.ExecuteRuleAsync(rule, sourceValue, context);

                if (!result.Success)
                {
                    _logger.LogWarning(
                        "Rule {RuleName} execution failed: {Error}",
                        rule.RuleName, result.ErrorMessage);
                    continue;
                }

                if (result.ExecutionDurationMs > rule.MaxExecutionTimeMs)
                {
                    _logger.LogWarning(
                        "Rule {RuleName} exceeded execution time: {Duration}ms (limit: {Limit}ms)",
                        rule.RuleName, Math.Round(result.ExecutionDurationMs, 2), rule.MaxExecutionTimeMs);
                }

                var virtualPointId = $"VIRTUAL_{rule.RuleId}";
                virtualMessages.Add(new MqttMessage
                {
                    MessageId = Guid.NewGuid().ToString(),
                    DeviceId = device.DeviceId,
                    PointId = virtualPointId,
                    PointName = rule.OutputPointName,
                    Value = result.Value,
                    Unit = rule.OutputUnit,
                    Timestamp = result.Timestamp,
                    Quality = 192,
                    DataType = "Double"
                });
            }
            catch (Exception ex)
            {
                _logger.LogError(ex, "Unexpected error applying rule {RuleName}", rule.RuleName);
            }
        }

        return virtualMessages;
    }

    private void RuleEngineService_OnRuleAutoDisabled(object? sender, RuleDisabledEventArgs e)
    {
        _logger.LogWarning(
            "Rule {RuleName} (Id: {RuleId}) auto-disabled: {Reason}",
            e.RuleName, e.RuleId, e.Reason);
    }
}