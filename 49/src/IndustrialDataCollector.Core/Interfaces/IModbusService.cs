using IndustrialDataCollector.Core.Entities;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IModbusService
{
    Task<bool> ConnectAsync(string ipAddress, int port, byte slaveId);
    Task DisconnectAsync();
    Task<bool> IsConnectedAsync();
    Task<ushort[]> ReadHoldingRegistersAsync(ushort startAddress, ushort count);
    Task<ushort[]> ReadInputRegistersAsync(ushort startAddress, ushort count);
    Task<bool[]> ReadCoilsAsync(ushort startAddress, ushort count);
    Task<bool[]> ReadDiscreteInputsAsync(ushort startAddress, ushort count);
    Task WriteSingleCoilAsync(ushort address, bool value);
    Task WriteMultipleCoilsAsync(ushort startAddress, bool[] values);
    Task WriteSingleRegisterAsync(ushort address, ushort value);
    Task WriteMultipleRegistersAsync(ushort startAddress, ushort[] values);
    event EventHandler? ConnectionLost;
    event EventHandler? ConnectionRestored;
}
