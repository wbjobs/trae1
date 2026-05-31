using MQTTnet;
using MQTTnet.Client;
using MQTTnet.Protocol;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using Microsoft.Extensions.Logging;
using System.Text;
using System.Text.Json;

namespace OpcUaGateway.Infrastructure.Mqtt;

public class MqttClientService : IMqttClientService
{
    private readonly ILogger<MqttClientService> _logger;
    private IMqttClient? _mqttClient;
    private readonly object _lock = new();
    private bool _disposed;
    private string _topicPrefix = "factory/gateway";

    public MqttClientService(ILogger<MqttClientService> logger)
    {
        _logger = logger;
    }

    public event EventHandler<MqttConnectionStateEventArgs>? ConnectionStateChanged;

    public async Task<bool> ConnectAsync(string brokerAddress, int port, string clientId, string? username = null, string? password = null)
    {
        try
        {
            var factory = new MqttFactory();
            _mqttClient = factory.CreateMqttClient();

            _mqttClient.ConnectedAsync += e =>
            {
                _logger.LogInformation("Connected to MQTT broker at {BrokerAddress}:{Port}", brokerAddress, port);
                OnConnectionStateChanged(true, "Connected");
                return Task.CompletedTask;
            };

            _mqttClient.DisconnectedAsync += e =>
            {
                _logger.LogWarning("Disconnected from MQTT broker: {Reason}", e.Reason);
                OnConnectionStateChanged(false, e.Reason.ToString());
                return Task.CompletedTask;
            };

            var options = new MqttClientOptionsBuilder()
                .WithClientId(clientId)
                .WithTcpServer(brokerAddress, port)
                .WithCleanSession()
                .WithKeepAlivePeriod(TimeSpan.FromSeconds(30))
                .WithTimeout(TimeSpan.FromSeconds(10));

            if (!string.IsNullOrEmpty(username))
            {
                options = options.WithCredentials(username, password ?? string.Empty);
            }

            var mqttOptions = options.Build();
            var result = await _mqttClient.ConnectAsync(mqttOptions);

            return result.ResultCode == MqttClientConnectResultCode.Success;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to connect to MQTT broker at {BrokerAddress}:{Port}", brokerAddress, port);
            OnConnectionStateChanged(false, ex.Message);
            return false;
        }
    }

    public async Task DisconnectAsync()
    {
        lock (_lock)
        {
            if (_mqttClient != null && _mqttClient.IsConnected)
            {
                try
                {
                    var disconnectOptions = new MqttClientDisconnectOptionsBuilder()
                        .WithReason(MqttClientDisconnectOptionsReason.NormalDisconnection)
                        .Build();

                    _mqttClient.DisconnectAsync(disconnectOptions).Wait(TimeSpan.FromSeconds(5));
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error disconnecting from MQTT broker");
                }
            }
        }
        await Task.CompletedTask;
    }

    public Task<bool> IsConnectedAsync()
    {
        return Task.FromResult(_mqttClient != null && _mqttClient.IsConnected);
    }

    public async Task<bool> PublishAsync(string topic, MqttMessage message)
    {
        if (_mqttClient == null || !_mqttClient.IsConnected)
        {
            _logger.LogWarning("Cannot publish: MQTT client is not connected");
            return false;
        }

        try
        {
            var fullTopic = $"{_topicPrefix}/{topic}";
            var payload = message.ToJson();

            var mqttMessage = new MqttApplicationMessageBuilder()
                .WithTopic(fullTopic)
                .WithPayload(Encoding.UTF8.GetBytes(payload))
                .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                .WithRetainFlag(false)
                .Build();

            var result = await _mqttClient.PublishAsync(mqttMessage);
            return result.IsSuccess;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error publishing MQTT message to topic {Topic}", topic);
            return false;
        }
    }

    public async Task<bool> PublishBatchAsync(string topic, IEnumerable<MqttMessage> messages)
    {
        if (_mqttClient == null || !_mqttClient.IsConnected)
        {
            _logger.LogWarning("Cannot publish batch: MQTT client is not connected");
            return false;
        }

        try
        {
            var fullTopic = $"{_topicPrefix}/{topic}";
            var messageList = messages.ToList();

            foreach (var message in messageList)
            {
                var payload = message.ToJson();
                var mqttMessage = new MqttApplicationMessageBuilder()
                    .WithTopic(fullTopic)
                    .WithPayload(Encoding.UTF8.GetBytes(payload))
                    .WithQualityOfServiceLevel(MqttQualityOfServiceLevel.AtLeastOnce)
                    .WithRetainFlag(false)
                    .Build();

                await _mqttClient.PublishAsync(mqttMessage);
            }

            _logger.LogDebug("Published batch of {Count} messages to {Topic}", messageList.Count, fullTopic);
            return true;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error publishing MQTT batch to topic {Topic}", topic);
            return false;
        }
    }

    private void OnConnectionStateChanged(bool isConnected, string? message)
    {
        ConnectionStateChanged?.Invoke(this, new MqttConnectionStateEventArgs
        {
            IsConnected = isConnected,
            Message = message,
            Timestamp = DateTime.UtcNow
        });
    }

    public async ValueTask DisposeAsync()
    {
        if (_disposed) return;
        _disposed = true;
        await DisconnectAsync();
        _mqttClient?.Dispose();
        GC.SuppressFinalize(this);
    }
}