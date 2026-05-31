using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface IMqttClientService : IAsyncDisposable
{
    Task<bool> ConnectAsync(string brokerAddress, int port, string clientId, string? username = null, string? password = null);
    Task DisconnectAsync();
    Task<bool> IsConnectedAsync();
    Task<bool> PublishAsync(string topic, MqttMessage message);
    Task<bool> PublishBatchAsync(string topic, IEnumerable<MqttMessage> messages);
    event EventHandler<MqttConnectionStateEventArgs>? ConnectionStateChanged;
}

public class MqttConnectionStateEventArgs : EventArgs
{
    public bool IsConnected { get; set; }
    public string? Message { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}