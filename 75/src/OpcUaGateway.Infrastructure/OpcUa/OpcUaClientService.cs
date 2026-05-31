using System.Security.Cryptography.X509Certificates;
using Opc.Ua;
using Opc.Ua.Client;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using OpcUaGateway.Core.Configuration;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace OpcUaGateway.Infrastructure.OpcUa;

public class OpcUaClientService : IOpcUaClient
{
    private readonly ILogger<OpcUaClientService> _logger;
    private readonly ICertificateManager _certificateManager;
    private readonly IEmailNotificationService? _emailService;
    private readonly CertificateConfig _certConfig;

    private Session? _session;
    private readonly object _lock = new();
    private bool _disposed;
    private CertificateInfo? _currentServerCertificate;
    private string? _currentEndpointUrl;
    private string? _currentUsername;
    private string? _currentPassword;
    private bool _currentUseSecurity;

    public OpcUaClientService(
        ILogger<OpcUaClientService> logger,
        ICertificateManager certificateManager,
        IOptions<GatewayConfig> config,
        IEmailNotificationService? emailService = null)
    {
        _logger = logger;
        _certificateManager = certificateManager;
        _certConfig = config.Value.Certificate;
        _emailService = emailService;
    }

    public CertificateInfo? CurrentServerCertificate => _currentServerCertificate;

    public event EventHandler<ConnectionStateChangedEventArgs>? ConnectionStateChanged;
    public event EventHandler<ServerCertificateChangedEventArgs>? ServerCertificateChanged;

    public async Task<bool> ConnectAsync(string endpointUrl, string? username = null, string? password = null, bool useSecurity = false)
    {
        _currentEndpointUrl = endpointUrl;
        _currentUsername = username;
        _currentPassword = password;
        _currentUseSecurity = useSecurity;

        return await InternalConnectAsync(endpointUrl, username, password, useSecurity, isReconnect: false);
    }

    private async Task<bool> InternalConnectAsync(string endpointUrl, string? username, string? password, bool useSecurity, bool isReconnect)
    {
        try
        {
            var endpoint = new ConfiguredEndpoint(null, new EndpointDescription(endpointUrl));

            var configuration = new ApplicationConfiguration
            {
                ApplicationName = "OpcUaGateway",
                ApplicationType = ApplicationType.Client,
                SecurityConfiguration = new SecurityConfiguration
                {
                    AutoAcceptUntrustedCertificates = false,
                    RejectSHA1SignedCertificates = false,
                    RejectUnknownRevocationStatus = false,
                },
                TransportConfigurations = new TransportConfiguration(),
                TransportQuotas = new TransportQuotas { OperationTimeout = 15000 },
                ClientConfiguration = new ClientConfiguration { DefaultSessionTimeout = 60000 }
            };

            await configuration.Validate(ApplicationType.Client);

            configuration.CertificateValidator.CertificateValidation += async (s, e) =>
            {
                var cert = ExtractCertificateInfo(e.Certificate);

                if (cert != null)
                {
                    var deviceId = _currentEndpointUrl ?? endpointUrl;
                    var deviceName = _currentEndpointUrl ?? endpointUrl;
                    var isAccepted = await _certificateManager.ValidateCertificateAsync(deviceId, deviceName, cert);

                    if (_currentServerCertificate != null && _currentServerCertificate.Thumbprint != cert.Thumbprint)
                    {
                        _logger.LogInformation("Certificate change detected for {Endpoint}: old={OldThumbprint}, new={NewThumbprint}",
                            endpointUrl, _currentServerCertificate.Thumbprint, cert.Thumbprint);

                        ServerCertificateChanged?.Invoke(this, new ServerCertificateChangedEventArgs
                        {
                            EndpointUrl = endpointUrl,
                            OldCertificate = _currentServerCertificate,
                            NewCertificate = cert,
                            ChangeTime = DateTime.UtcNow,
                            IsAutoRenegotiated = _certConfig.AutoRenegotiateOnChange
                        });

                        if (_emailService != null)
                        {
                            _ = _emailService.SendCertificateChangedNotificationAsync(
                                deviceId, deviceName, _currentServerCertificate, cert, isAccepted);
                        }
                    }

                    _currentServerCertificate = cert;

                    e.Accept = isAccepted;

                    if (!isAccepted)
                    {
                        _logger.LogWarning("Certificate rejected for {Endpoint}: {Thumbprint} ({Subject})",
                            endpointUrl, cert.Thumbprint, cert.Subject);

                        if (_emailService != null)
                        {
                            _ = _emailService.SendCertificateRejectedNotificationAsync(
                                deviceId, deviceName, cert, "Rejected by certificate acceptance policy");
                        }
                    }
                }
                else
                {
                    e.Accept = e.Error.StatusCode == StatusCodes.BadCertificateUntrusted;
                }
            };

            var userIdentity = !string.IsNullOrEmpty(username)
                ? new UserIdentity(username, password)
                : new UserIdentity();

            _session = await Session.Create(
                configuration,
                endpoint,
                false,
                false,
                "OpcUaGateway",
                60000,
                userIdentity,
                null);

            if (_session != null && _session.Connected)
            {
                var msg = isReconnect ? "Reconnected" : "Connected";
                _logger.LogInformation("{Msg} to OPC UA server at {EndpointUrl}", msg, endpointUrl);
                OnConnectionStateChanged(true, msg);

                if (_currentServerCertificate != null && _currentServerCertificate.DaysUntilExpiry <= _certConfig.ExpiryWarningDays)
                {
                    _logger.LogWarning("Server certificate for {Endpoint} expires in {Days} days (expiry: {ExpiryDate})",
                        endpointUrl, _currentServerCertificate.DaysUntilExpiry, _currentServerCertificate.NotAfter);
                }

                return true;
            }

            return false;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to connect to OPC UA server at {EndpointUrl}", endpointUrl);
            OnConnectionStateChanged(false, ex.Message);
            return false;
        }
    }

    public async Task<bool> ReconnectAsync()
    {
        if (_session != null)
        {
            try
            {
                if (_session.Connected)
                {
                    _session.Close();
                }
                _session.Dispose();
                _session = null;
            }
            catch (Exception ex)
            {
                _logger.LogWarning(ex, "Error closing session during reconnect");
                _session = null;
            }
        }

        return await InternalConnectAsync(_currentEndpointUrl ?? string.Empty,
            _currentUsername, _currentPassword, _currentUseSecurity, isReconnect: true);
    }

    public async Task DisconnectAsync()
    {
        lock (_lock)
        {
            if (_session != null)
            {
                try
                {
                    if (_session.Connected)
                    {
                        _session.Close();
                    }
                    _session.Dispose();
                    _session = null;
                    OnConnectionStateChanged(false, "Disconnected");
                }
                catch (Exception ex)
                {
                    _logger.LogError(ex, "Error disconnecting from OPC UA server");
                }
            }
        }
        await Task.CompletedTask;
    }

    public Task<bool> IsConnectedAsync()
    {
        return Task.FromResult(_session != null && _session.Connected);
    }

    public async Task<Dictionary<string, (double Value, int Quality, DateTime Timestamp)>> ReadNodesAsync(IEnumerable<string> nodeIds)
    {
        var results = new Dictionary<string, (double Value, int Quality, DateTime Timestamp)>();

        if (_session == null || !_session.Connected)
        {
            _logger.LogWarning("Cannot read nodes: session is not connected");
            return results;
        }

        try
        {
            var nodesToRead = new ReadValueIdCollection();
            var nodeIdList = nodeIds.ToList();

            foreach (var nodeId in nodeIdList)
            {
                nodesToRead.Add(new ReadValueId
                {
                    NodeId = new NodeId(nodeId),
                    AttributeId = Attributes.Value
                });
            }

            _session.Read(
                null,
                0,
                TimestampsToReturn.Both,
                nodesToRead,
                out var readResults,
                out var diagnosticInfos);

            ClientBase.ValidateResponse(readResults, nodesToRead);
            ClientBase.ValidateDiagnosticInfos(diagnosticInfos, nodesToRead);

            for (int i = 0; i < readResults.Count && i < nodeIdList.Count; i++)
            {
                var result = readResults[i];
                var nodeId = nodeIdList[i];

                if (StatusCode.IsGood(result.StatusCode))
                {
                    var value = result.Value;
                    double doubleValue = value switch
                    {
                        double d => d,
                        float f => f,
                        int i32 => i32,
                        long i64 => i64,
                        short i16 => i16,
                        byte b => b,
                        bool b2 => b2 ? 1.0 : 0.0,
                        _ => Convert.ToDouble(value)
                    };

                    results[nodeId] = (doubleValue, (int)result.StatusCode.Code, result.SourceTimestamp);
                }
                else
                {
                    results[nodeId] = (0.0, (int)result.StatusCode.Code, DateTime.UtcNow);
                }
            }
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading nodes from OPC UA server");
        }

        return results;
    }

    public async Task<bool> TestConnectionAsync(string endpointUrl, string? username = null, string? password = null)
    {
        try
        {
            var endpoint = new ConfiguredEndpoint(null, new EndpointDescription(endpointUrl));

            var configuration = new ApplicationConfiguration
            {
                ApplicationName = "OpcUaGateway",
                ApplicationType = ApplicationType.Client,
                SecurityConfiguration = new SecurityConfiguration
                {
                    AutoAcceptUntrustedCertificates = false,
                    RejectSHA1SignedCertificates = false,
                    RejectUnknownRevocationStatus = false,
                },
                TransportQuotas = new TransportQuotas { OperationTimeout = 10000 },
                ClientConfiguration = new ClientConfiguration { DefaultSessionTimeout = 30000 }
            };

            await configuration.Validate(ApplicationType.Client);

            configuration.CertificateValidator.CertificateValidation += async (s, e) =>
            {
                var cert = ExtractCertificateInfo(e.Certificate);
                if (cert != null)
                {
                    var isAccepted = await _certificateManager.ValidateCertificateAsync(
                        endpointUrl, endpointUrl, cert);
                    e.Accept = isAccepted;
                }
                else
                {
                    e.Accept = e.Error.StatusCode == StatusCodes.BadCertificateUntrusted;
                }
            };

            var userIdentity = !string.IsNullOrEmpty(username)
                ? new UserIdentity(username, password)
                : new UserIdentity();

            using var testSession = await Session.Create(
                configuration,
                endpoint,
                false,
                false,
                "OpcUaGateway_Test",
                30000,
                userIdentity,
                null);

            return testSession != null && testSession.Connected;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Connection test failed for {EndpointUrl}", endpointUrl);
            return false;
        }
    }

    private CertificateInfo? ExtractCertificateInfo(X509Certificate2? certificate)
    {
        if (certificate == null) return null;

        try
        {
            return new CertificateInfo
            {
                Thumbprint = certificate.Thumbprint ?? string.Empty,
                Subject = certificate.Subject ?? string.Empty,
                Issuer = certificate.Issuer ?? string.Empty,
                NotBefore = certificate.NotBefore,
                NotAfter = certificate.NotAfter,
                SerialNumber = certificate.SerialNumber ?? string.Empty,
                SignatureAlgorithm = certificate.SignatureAlgorithm?.FriendlyName ?? "Unknown",
                IsSelfSigned = certificate.Subject == certificate.Issuer
            };
        }
        catch (Exception ex)
        {
            _logger.LogWarning(ex, "Failed to extract certificate info");
            return null;
        }
    }

    private void OnConnectionStateChanged(bool isConnected, string? message)
    {
        ConnectionStateChanged?.Invoke(this, new ConnectionStateChangedEventArgs
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
        GC.SuppressFinalize(this);
    }
}