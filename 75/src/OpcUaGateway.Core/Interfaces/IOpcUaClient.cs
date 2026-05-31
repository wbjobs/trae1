using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Core.Interfaces;

public interface IOpcUaClient : IAsyncDisposable
{
    Task<bool> ConnectAsync(string endpointUrl, string? username = null, string? password = null, bool useSecurity = false);
    Task DisconnectAsync();
    Task<bool> IsConnectedAsync();
    Task<Dictionary<string, (double Value, int Quality, DateTime Timestamp)>> ReadNodesAsync(IEnumerable<string> nodeIds);
    Task<bool> TestConnectionAsync(string endpointUrl, string? username = null, string? password = null);
    Task<bool> ReconnectAsync();
    CertificateInfo? CurrentServerCertificate { get; }
    event EventHandler<ConnectionStateChangedEventArgs>? ConnectionStateChanged;
    event EventHandler<ServerCertificateChangedEventArgs>? ServerCertificateChanged;
}

public class ConnectionStateChangedEventArgs : EventArgs
{
    public bool IsConnected { get; set; }
    public string? Message { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.UtcNow;
}

public class ServerCertificateChangedEventArgs : EventArgs
{
    public string EndpointUrl { get; set; } = string.Empty;
    public CertificateInfo OldCertificate { get; set; } = new();
    public CertificateInfo NewCertificate { get; set; } = new();
    public DateTime ChangeTime { get; set; } = DateTime.UtcNow;
    public bool IsAutoRenegotiated { get; set; }
}