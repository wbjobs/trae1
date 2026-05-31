using IndustrialDataCollector.Core.DTOs;
using IndustrialDataCollector.Core.Interfaces;
using IndustrialDataCollector.Infrastructure.Services;
using Microsoft.AspNetCore.Mvc;

namespace IndustrialDataCollector.Api.Controllers;

[ApiController]
[Route("api/[controller]")]
public class DevicesController : ControllerBase
{
    private readonly DeviceService _deviceService;
    private readonly IDataStorage _dataStorage;

    public DevicesController(
        DeviceService deviceService,
        IDataStorage dataStorage)
    {
        _deviceService = deviceService;
        _dataStorage = dataStorage;
    }

    [HttpGet]
    public async Task<ActionResult<IEnumerable<DeviceDto>>> GetDevices()
    {
        var devices = await _deviceService.GetAllDevicesAsync();
        return Ok(devices);
    }

    [HttpGet("{id}")]
    public async Task<ActionResult<DeviceDto>> GetDevice(int id)
    {
        var device = await _deviceService.GetDeviceByIdAsync(id);
        if (device == null) return NotFound();
        return Ok(device);
    }

    [HttpPost]
    public async Task<ActionResult<DeviceDto>> CreateDevice([FromBody] DeviceCreateDto dto)
    {
        var device = await _deviceService.AddDeviceAsync(dto);
        return CreatedAtAction(nameof(GetDevice), new { id = device.Id }, device);
    }

    [HttpPut("{id}")]
    public async Task<ActionResult<DeviceDto>> UpdateDevice(int id, [FromBody] DeviceUpdateDto dto)
    {
        var device = await _deviceService.UpdateDeviceAsync(id, dto);
        if (device == null) return NotFound();
        return Ok(device);
    }

    [HttpDelete("{id}")]
    public async Task<ActionResult> DeleteDevice(int id)
    {
        var result = await _deviceService.DeleteDeviceAsync(id);
        if (!result) return NotFound();
        return NoContent();
    }

    [HttpGet("{id}/offline-logs")]
    public async Task<ActionResult<IEnumerable<OfflineLogDto>>> GetOfflineLogs(int id)
    {
        var logs = await _deviceService.GetOfflineLogsAsync(id);
        return Ok(logs);
    }

    [HttpGet("offline-logs")]
    public async Task<ActionResult<IEnumerable<OfflineLogDto>>> GetAllOfflineLogs()
    {
        var logs = await _deviceService.GetOfflineLogsAsync();
        return Ok(logs);
    }

    [HttpGet("{id}/data")]
    public async Task<ActionResult<IEnumerable<DataPointDto>>> GetDeviceData(
        int id,
        [FromQuery] DateTime? startTime,
        [FromQuery] DateTime? endTime)
    {
        var device = await _deviceService.GetDeviceByIdAsync(id);
        if (device == null) return NotFound();

        var start = startTime ?? DateTime.UtcNow.AddHours(-1);
        var end = endTime ?? DateTime.UtcNow;

        var dataPoints = await _dataStorage.QueryDataAsync(
            "sensor_data",
            start,
            end,
            device.Name);

        var dtos = dataPoints.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });

        return Ok(dtos);
    }

    [HttpGet("{id}/data/latest")]
    public async Task<ActionResult<IEnumerable<DataPointDto>>> GetLatestDeviceData(int id, [FromQuery] int limit = 100)
    {
        var device = await _deviceService.GetDeviceByIdAsync(id);
        if (device == null) return NotFound();

        var dataPoints = await _dataStorage.GetLatestDataAsync("sensor_data", device.Name, limit);

        var dtos = dataPoints.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });

        return Ok(dtos);
    }

    [HttpPost("data/query")]
    public async Task<ActionResult<IEnumerable<DataPointDto>>> QueryData([FromBody] DataQueryDto dto)
    {
        var dataPoints = await _dataStorage.QueryDataAsync(
            dto.Measurement,
            dto.StartTime,
            dto.EndTime);

        var filteredData = dataPoints.AsEnumerable();

        if (dto.DeviceIds != null && dto.DeviceIds.Any())
        {
            filteredData = filteredData.Where(dp => dto.DeviceIds.Contains(dp.DeviceId));
        }

        if (dto.RegisterNames != null && dto.RegisterNames.Any())
        {
            filteredData = filteredData.Where(dp => dto.RegisterNames.Contains(dp.RegisterName));
        }

        var dtos = filteredData.Select(dp => new DataPointDto
        {
            DeviceId = dp.DeviceId,
            DeviceName = dp.DeviceName,
            RegisterName = dp.RegisterName,
            Value = dp.Value,
            Unit = dp.Unit,
            Timestamp = dp.Timestamp
        });

        return Ok(dtos);
    }
}
