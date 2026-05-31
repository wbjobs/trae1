using IndustrialDataCollector.Core.Enums;

namespace IndustrialDataCollector.Core.DTOs;

public class RegisterMapDto
{
    public int Id { get; set; }
    public int DeviceId { get; set; }
    public string Name { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public RegisterType RegisterType { get; set; }
    public ushort Address { get; set; }
    public ushort Length { get; set; } = 1;
    public double ScaleFactor { get; set; } = 1.0;
    public double Offset { get; set; } = 0.0;
    public string Unit { get; set; } = string.Empty;
    public string DataType { get; set; } = "float";
    public bool IsActive { get; set; } = true;
    public double? UpperThreshold { get; set; }
    public double? LowerThreshold { get; set; }
    public bool EnableAlarm { get; set; }
    public ushort? ControlCoilAddress { get; set; }
    public bool ControlCoilValue { get; set; }
    public string? ControlDescription { get; set; }
}

public class RegisterMapCreateDto
{
    public string Name { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public RegisterType RegisterType { get; set; }
    public ushort Address { get; set; }
    public ushort Length { get; set; } = 1;
    public double ScaleFactor { get; set; } = 1.0;
    public double Offset { get; set; } = 0.0;
    public string Unit { get; set; } = string.Empty;
    public string DataType { get; set; } = "float";
    public bool IsActive { get; set; } = true;
    public double? UpperThreshold { get; set; }
    public double? LowerThreshold { get; set; }
    public bool EnableAlarm { get; set; }
    public ushort? ControlCoilAddress { get; set; }
    public bool ControlCoilValue { get; set; }
    public string? ControlDescription { get; set; }
}
