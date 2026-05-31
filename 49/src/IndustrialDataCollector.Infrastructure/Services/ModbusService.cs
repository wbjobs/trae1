using System.Net.Sockets;
using IndustrialDataCollector.Core.Interfaces;
using Microsoft.Extensions.Logging;
using NModbus;

namespace IndustrialDataCollector.Infrastructure.Services;

public class ModbusService : IModbusService, IDisposable
{
    private readonly ILogger<ModbusService> _logger;
    private TcpClient? _tcpClient;
    private IModbusMaster? _master;
    private string? _ipAddress;
    private int _port;
    private byte _slaveId;
    private bool _disposed;

    public event EventHandler? ConnectionLost;
    public event EventHandler? ConnectionRestored;

    public ModbusService(ILogger<ModbusService> logger)
    {
        _logger = logger;
    }

    public async Task<bool> ConnectAsync(string ipAddress, int port, byte slaveId)
    {
        try
        {
            _ipAddress = ipAddress;
            _port = port;
            _slaveId = slaveId;

            _tcpClient = new TcpClient();
            await _tcpClient.ConnectAsync(ipAddress, port);

            var factory = new ModbusFactory();
            _master = factory.CreateMaster(_tcpClient);

            _logger.LogInformation("Successfully connected to Modbus device at {IpAddress}:{Port}, SlaveId: {SlaveId}", 
                ipAddress, port, slaveId);

            ConnectionRestored?.Invoke(this, EventArgs.Empty);
            return true;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Failed to connect to Modbus device at {IpAddress}:{Port}", ipAddress, port);
            _tcpClient?.Close();
            _tcpClient = null;
            _master = null;
            return false;
        }
    }

    public async Task DisconnectAsync()
    {
        try
        {
            if (_master != null)
            {
                _master.Dispose();
                _master = null;
            }

            if (_tcpClient != null)
            {
                if (_tcpClient.Connected)
                {
                    await Task.Run(() => _tcpClient.Close());
                }
                _tcpClient.Dispose();
                _tcpClient = null;
            }

            _logger.LogInformation("Disconnected from Modbus device");
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error disconnecting from Modbus device");
        }
    }

    public async Task<bool> IsConnectedAsync()
    {
        return await Task.Run(() => 
            _tcpClient != null && _tcpClient.Connected && _master != null);
    }

    public async Task<ushort[]> ReadHoldingRegistersAsync(ushort startAddress, ushort count)
    {
        EnsureConnected();

        try
        {
            return await Task.Run(() => 
                _master!.ReadHoldingRegisters(_slaveId, startAddress, count));
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading holding registers from {StartAddress}, count: {Count}", 
                startAddress, count);
            throw;
        }
    }

    public async Task<ushort[]> ReadInputRegistersAsync(ushort startAddress, ushort count)
    {
        EnsureConnected();

        try
        {
            return await Task.Run(() => 
                _master!.ReadInputRegisters(_slaveId, startAddress, count));
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading input registers from {StartAddress}, count: {Count}", 
                startAddress, count);
            throw;
        }
    }

    public async Task<bool[]> ReadCoilsAsync(ushort startAddress, ushort count)
    {
        EnsureConnected();

        try
        {
            return await Task.Run(() => 
                _master!.ReadCoils(_slaveId, startAddress, count));
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading coils from {StartAddress}, count: {Count}", 
                startAddress, count);
            throw;
        }
    }

    public async Task<bool[]> ReadDiscreteInputsAsync(ushort startAddress, ushort count)
    {
        EnsureConnected();

        try
        {
            return await Task.Run(() => 
                _master!.ReadInputs(_slaveId, startAddress, count));
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error reading discrete inputs from {StartAddress}, count: {Count}", 
                startAddress, count);
            throw;
        }
    }

    public async Task WriteSingleCoilAsync(ushort address, bool value)
    {
        EnsureConnected();

        try
        {
            await Task.Run(() => 
                _master!.WriteSingleCoil(_slaveId, address, value));
            _logger.LogDebug("Written coil at {Address}: {Value}", address, value);
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error writing single coil at {Address}: {Value}", address, value);
            throw;
        }
    }

    public async Task WriteMultipleCoilsAsync(ushort startAddress, bool[] values)
    {
        EnsureConnected();

        try
        {
            await Task.Run(() => 
                _master!.WriteMultipleCoils(_slaveId, startAddress, values));
            _logger.LogDebug("Written {Count} coils starting at {StartAddress}", values.Length, startAddress);
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error writing multiple coils starting at {StartAddress}", startAddress);
            throw;
        }
    }

    public async Task WriteSingleRegisterAsync(ushort address, ushort value)
    {
        EnsureConnected();

        try
        {
            await Task.Run(() => 
                _master!.WriteSingleRegister(_slaveId, address, value));
            _logger.LogDebug("Written register at {Address}: {Value}", address, value);
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error writing single register at {Address}: {Value}", address, value);
            throw;
        }
    }

    public async Task WriteMultipleRegistersAsync(ushort startAddress, ushort[] values)
    {
        EnsureConnected();

        try
        {
            await Task.Run(() => 
                _master!.WriteMultipleRegisters(_slaveId, startAddress, values));
            _logger.LogDebug("Written {Count} registers starting at {StartAddress}", values.Length, startAddress);
        }
        catch (IOException ex)
        {
            HandleConnectionError(ex);
            throw;
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error writing multiple registers starting at {StartAddress}", startAddress);
            throw;
        }
    }

    private void EnsureConnected()
    {
        if (_master == null || _tcpClient == null || !_tcpClient.Connected)
        {
            throw new InvalidOperationException("Modbus client is not connected");
        }
    }

    private void HandleConnectionError(IOException ex)
    {
        _logger.LogWarning(ex, "Connection lost to Modbus device");
        ConnectionLost?.Invoke(this, EventArgs.Empty);
    }

    public async Task<bool> ReconnectAsync()
    {
        if (_ipAddress == null)
        {
            return false;
        }

        _logger.LogInformation("Attempting to reconnect to Modbus device at {IpAddress}:{Port}", 
            _ipAddress, _port);

        await DisconnectAsync();
        return await ConnectAsync(_ipAddress, _port, _slaveId);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _master?.Dispose();
        _tcpClient?.Dispose();
    }
}
