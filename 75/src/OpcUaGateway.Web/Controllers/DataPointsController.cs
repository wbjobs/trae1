using Microsoft.AspNetCore.Mvc;
using OpcUaGateway.Core.DTOs;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Web.Controllers;

[ApiController]
[Route("api/[controller]")]
public class DataPointsController : ControllerBase
{
    private readonly IConfigService _configService;

    public DataPointsController(IConfigService configService)
    {
        _configService = configService;
    }

    [HttpGet("{deviceId}")]
    public async Task<ActionResult<List<DataPoint>>> GetDataPoints(string deviceId)
    {
        var points = await _configService.GetDataPointsAsync(deviceId);
        return Ok(points);
    }

    [HttpGet("{deviceId}/{pointId}")]
    public async Task<ActionResult<DataPoint>> GetDataPoint(string deviceId, string pointId)
    {
        var point = await _configService.GetDataPointAsync(deviceId, pointId);
        if (point == null) return NotFound();
        return Ok(point);
    }

    [HttpPost("{deviceId}")]
    public async Task<ActionResult<DataPoint>> AddDataPoint(string deviceId, [FromBody] DataPointDto dto)
    {
        var point = await _configService.AddDataPointAsync(deviceId, dto);
        return CreatedAtAction(nameof(GetDataPoint), new { deviceId, pointId = point.PointId }, point);
    }

    [HttpPut("{deviceId}/{pointId}")]
    public async Task<ActionResult<DataPoint>> UpdateDataPoint(string deviceId, string pointId, [FromBody] DataPointDto dto)
    {
        var point = await _configService.UpdateDataPointAsync(deviceId, pointId, dto);
        if (point == null) return NotFound();
        return Ok(point);
    }

    [HttpDelete("{deviceId}/{pointId}")]
    public async Task<IActionResult> DeleteDataPoint(string deviceId, string pointId)
    {
        var result = await _configService.DeleteDataPointAsync(deviceId, pointId);
        if (!result) return NotFound();
        return NoContent();
    }
}