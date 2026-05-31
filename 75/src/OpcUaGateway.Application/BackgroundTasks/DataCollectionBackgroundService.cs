using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Configuration;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Options;
using Microsoft.Extensions.Logging;

namespace OpcUaGateway.Application.BackgroundTasks;

public class DataCollectionBackgroundService : BackgroundService
{
    private readonly ILogger<DataCollectionBackgroundService> _logger;
    private readonly IDataCollectionService _dataCollectionService;
    private readonly IMqttClientService _mqttClient;
    private readonly IOfflineRepository _offlineRepository;
    private readonly GatewayConfig _config;

    public DataCollectionBackgroundService(
        ILogger<DataCollectionBackgroundService> logger,
        IDataCollectionService dataCollectionService,
        IMqttClientService mqttClient,
        IOfflineRepository offlineRepository,
        IOptions<GatewayConfig> config)
    {
        _logger = logger;
        _dataCollectionService = dataCollectionService;
        _mqttClient = mqttClient;
        _offlineRepository = offlineRepository;
        _config = config.Value;
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        _logger.LogInformation("Data Collection Background Service is starting");

        try
        {
            await _offlineRepository.InitializeAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to initialize offline repository");
        }

        try
        {
            var mqttConnected = await _mqttClient.ConnectAsync(
                _config.Mqtt.BrokerAddress,
                _config.Mqtt.Port,
                _config.Mqtt.ClientId,
                _config.Mqtt.Username,
                _config.Mqtt.Password);

            if (mqttConnected)
            {
                _logger.LogInformation("Connected to MQTT broker at {BrokerAddress}:{Port}",
                    _config.Mqtt.BrokerAddress, _config.Mqtt.Port);
            }
            else
            {
                _logger.LogWarning("Failed to connect to MQTT broker, will cache data locally");
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error connecting to MQTT broker");
        }

        await _dataCollectionService.StartAsync(stoppingToken);

        _logger.LogInformation("Data Collection Background Service is running");
    }

    public override async Task StopAsync(CancellationToken cancellationToken)
    {
        _logger.LogInformation("Data Collection Background Service is stopping");

        await _dataCollectionService.StopAsync();

        try
        {
            await _mqttClient.DisconnectAsync();
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error disconnecting MQTT client");
        }

        await base.StopAsync(cancellationToken);

        _logger.LogInformation("Data Collection Background Service has stopped");
    }
}