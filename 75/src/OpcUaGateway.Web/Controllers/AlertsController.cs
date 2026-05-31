using Microsoft.AspNetCore.Mvc;
using OpcUaGateway.Core.DTOs;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Web.Controllers;

[ApiController]
[Route("api/[controller]")]
public class AlertsController : ControllerBase
{
    private readonly IConfigService _configService;

    public AlertsController(IConfigService configService)
    {
        _configService = configService;
    }

    [HttpGet("active")]
    public async Task<ActionResult<List<AlertDto>>> GetActiveAlerts()
    {
        var alerts = await _configService.GetActiveAlertsAsync();
        var dtos = alerts.Select(a => new AlertDto
        {
            AlertId = a.AlertId,
            DeviceId = a.DeviceId,
            PointId = a.PointId,
            PointName = a.PointName,
            AlertType = a.AlertType.ToString(),
            ThresholdValue = a.ThresholdValue,
            ActualValue = a.ActualValue,
            TriggerTime = a.TriggerTime,
            IsAcknowledged = a.IsAcknowledged,
            Message = a.Message
        }).ToList();
        return Ok(dtos);
    }

    [HttpGet("history")]
    public async Task<ActionResult<List<AlertDto>>> GetAlertHistory([FromQuery] int hours = 24)
    {
        var alerts = await _configService.GetAlertHistoryAsync(hours);
        var dtos = alerts.Select(a => new AlertDto
        {
            AlertId = a.AlertId,
            DeviceId = a.DeviceId,
            PointId = a.PointId,
            PointName = a.PointName,
            AlertType = a.AlertType.ToString(),
            ThresholdValue = a.ThresholdValue,
            ActualValue = a.ActualValue,
            TriggerTime = a.TriggerTime,
            IsAcknowledged = a.IsAcknowledged,
            Message = a.Message
        }).ToList();
        return Ok(dtos);
    }

    [HttpPost("{alertId}/acknowledge")]
    public async Task<IActionResult> AcknowledgeAlert(string alertId)
    {
        var result = await _configService.AcknowledgeAlertAsync(alertId);
        if (!result) return NotFound();
        return NoContent();
    }

    [HttpPut("threshold")]
    public async Task<IActionResult> UpdateThreshold([FromBody] ThresholdConfigDto dto)
    {
        var point = await _configService.GetDataPointAsync(dto.DeviceId, dto.PointId);
        if (point == null) return NotFound();

        point.UpperThreshold = dto.UpperThreshold;
        point.LowerThreshold = dto.LowerThreshold;
        point.RateOfChangeThreshold = dto.RateOfChangeThreshold;

        await _configService.UpdateDataPointAsync(dto.DeviceId, dto.PointId, new DataPointDto
        {
            NodeId = point.NodeId,
            PointName = point.PointName,
            DataType = point.DataType,
            Unit = point.Unit,
            Description = point.Description,
            IsEnabled = point.IsEnabled,
            UpperThreshold = dto.UpperThreshold,
            LowerThreshold = dto.LowerThreshold,
            RateOfChangeThreshold = dto.RateOfChangeThreshold
        });

        return NoContent();
    }
}