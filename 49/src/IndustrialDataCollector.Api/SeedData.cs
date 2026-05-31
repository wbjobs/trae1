using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Api;

public static class SeedData
{
    public static async Task InitializeAsync(IServiceProvider serviceProvider)
    {
        var deviceRepository = serviceProvider.GetRequiredService<IDeviceRepository>();
        var existingDevices = await deviceRepository.GetAllAsync();

        if (existingDevices.Any()) return;

        var deviceTypes = new[]
        {
            new { Name = "PLC-001", Ip = "192.168.1.101", Desc = "生产线1-温度压力传感器" },
            new { Name = "PLC-002", Ip = "192.168.1.102", Desc = "生产线2-温度压力传感器" },
            new { Name = "PLC-003", Ip = "192.168.1.103", Desc = "生产线3-温度压力传感器" },
            new { Name = "PLC-004", Ip = "192.168.1.104", Desc = "生产线4-温度压力传感器" },
            new { Name = "PLC-005", Ip = "192.168.1.105", Desc = "生产线5-温度压力传感器" },
            new { Name = "PLC-006", Ip = "192.168.1.106", Desc = "生产线6-温度压力传感器" },
            new { Name = "PLC-007", Ip = "192.168.1.107", Desc = "生产线7-温度压力传感器" },
            new { Name = "PLC-008", Ip = "192.168.1.108", Desc = "生产线8-温度压力传感器" },
            new { Name = "PLC-009", Ip = "192.168.1.109", Desc = "生产线9-温度压力传感器" },
            new { Name = "PLC-010", Ip = "192.168.1.110", Desc = "生产线10-温度压力传感器" }
        };

        foreach (var deviceInfo in deviceTypes)
        {
            var device = new Device
            {
                Name = deviceInfo.Name,
                IpAddress = deviceInfo.Ip,
                Port = 502,
                SlaveId = 1,
                Description = deviceInfo.Desc,
                Status = DeviceStatus.Offline,
                IsActive = true,
                ReconnectIntervalSeconds = 30,
                MaxReconnectAttempts = 5,
                RegisterMaps = new List<RegisterMap>
                {
                    new()
                    {
                        Name = "Temperature",
                        Description = "温度传感器",
                        RegisterType = RegisterType.HoldingRegister,
                        Address = 0,
                        Length = 2,
                        ScaleFactor = 0.1,
                        Offset = 0,
                        Unit = "°C",
                        DataType = "float",
                        IsActive = true
                    },
                    new()
                    {
                        Name = "Pressure",
                        Description = "压力传感器",
                        RegisterType = RegisterType.HoldingRegister,
                        Address = 2,
                        Length = 2,
                        ScaleFactor = 0.01,
                        Offset = 0,
                        Unit = "MPa",
                        DataType = "float",
                        IsActive = true
                    },
                    new()
                    {
                        Name = "Speed",
                        Description = "转速传感器",
                        RegisterType = RegisterType.HoldingRegister,
                        Address = 4,
                        Length = 1,
                        ScaleFactor = 1.0,
                        Offset = 0,
                        Unit = "RPM",
                        DataType = "uint16",
                        IsActive = true
                    },
                    new()
                    {
                        Name = "Vibration",
                        Description = "振动传感器",
                        RegisterType = RegisterType.InputRegister,
                        Address = 0,
                        Length = 2,
                        ScaleFactor = 0.001,
                        Offset = 0,
                        Unit = "mm/s",
                        DataType = "float",
                        IsActive = true
                    }
                }
            };

            await deviceRepository.AddAsync(device);
        }
    }
}
