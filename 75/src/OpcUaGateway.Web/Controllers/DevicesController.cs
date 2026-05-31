using Microsoft.AspNetCore.Mvc;
using OpcUaGateway.Core.DTOs;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Web.Controllers;

[ApiController]
[Route("api/[controller]")]
public class DevicesController : ControllerBase
{
    private readonly IConfigService _configService;
    private readonly IDataCollectionService _dataCollectionService;

    public DevicesController(IConfigService configService, IDataCollectionService dataCollectionService)
    {
        _configService = configService;
        _dataCollectionService = dataCollectionService;
    }

    [HttpGet]
    public async Task<ActionResult<List<Device>>> GetDevices()
    {
        var devices = await _configService.GetDevicesAsync();
        return Ok(devices);
    }

    [HttpGet("{deviceId}")]
    public async Task<ActionResult<Device>> GetDevice(string deviceId)
    {
        var device = await _configService.GetDeviceAsync(deviceId);
        if (device == null) return NotFound();
        return Ok(device);
    }

    [HttpGet("statuses")]
    public async Task<ActionResult<List<DeviceStatusDto>>> GetDeviceStatuses()
    {
        var statuses = await _dataCollectionService.GetDeviceStatusesAsync();
        var dtos = statuses.Select(s => new DeviceStatusDto
        {
            DeviceId = s.DeviceId,
            DeviceName = s.DeviceName,
            Status = s.Status.ToString(),
            LastUpdateTime = s.LastUpdateTime,
            LastSuccessfulCollectTime = s.LastSuccessfulCollectTime,
            IsConnected = s.IsConnected,
            ErrorMessage = s.ErrorMessage,
            SuccessfulCollectCount = s.SuccessfulCollectCount,
            FailedCollectCount = s.FailedCollectCount
        }).ToList();
        return Ok(dtos);
    }

    [HttpGet("{deviceId}/status")]
    public async Task<ActionResult<DeviceStatusDto>> GetDeviceStatus(string deviceId)
    {
        var status = await _dataCollectionService.GetDeviceStatusAsync(deviceId);
        if (status == null) return NotFound();
        
        return Ok(new DeviceStatusDto
        {
            DeviceId = status.DeviceId,
            DeviceName = status.DeviceName,
            Status = status.Status.ToString(),
            LastUpdateTime = status.LastUpdateTime,
            LastSuccessfulCollectTime = status.LastSuccessfulCollectTime,
            IsConnected = status.IsConnected,
            ErrorMessage = status.ErrorMessage,
            SuccessfulCollectCount = status.SuccessfulCollectCount,
            FailedCollectCount = status.FailedCollectCount
        });
    }

    [HttpPost]
    public async Task<ActionResult<Device>> AddDevice([FromBody] DeviceConfigDto dto)
    {
        var device = await _configService.AddDeviceAsync(dto);
        return CreatedAtAction(nameof(GetDevice), new { deviceId = device.DeviceId }, device);
    }

    [HttpPut("{deviceId}")]
    public async Task<ActionResult<Device>> UpdateDevice(string deviceId, [FromBody] DeviceConfigDto dto)
    {
        var device = await _configService.UpdateDeviceAsync(deviceId, dto);
        if (device == null) return NotFound();
        return Ok(device);
    }

    [HttpDelete("{deviceId}")]
    public async Task<IActionResult> DeleteDevice(string deviceId)
    {
        var result = await _configService.DeleteDeviceAsync(deviceId);
        if (!result) return NotFound();
        return NoContent();
    }
}