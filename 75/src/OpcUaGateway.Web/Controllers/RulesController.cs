using Microsoft.AspNetCore.Mvc;
using OpcUaGateway.Core.DTOs;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;

namespace OpcUaGateway.Web.Controllers;

[ApiController]
[Route("api/[controller]")]
public class RulesController : ControllerBase
{
    private readonly IRuleService _ruleService;
    private readonly IRuleEngine _ruleEngine;
    private readonly ILogger<RulesController> _logger;

    public RulesController(
        IRuleService ruleService,
        IRuleEngine ruleEngine,
        ILogger<RulesController> logger)
    {
        _ruleService = ruleService;
        _ruleEngine = ruleEngine;
        _logger = logger;
    }

    [HttpGet]
    public async Task<ActionResult<List<RuleDto>>> GetRules(string? deviceId = null)
    {
        var rules = await _ruleService.GetRulesAsync(deviceId);
        var dtos = rules.Select(MapToDto).ToList();
        return Ok(dtos);
    }

    [HttpGet("{ruleId}")]
    public async Task<ActionResult<RuleDto>> GetRule(string ruleId)
    {
        var rule = await _ruleService.GetRuleAsync(ruleId);
        if (rule == null) return NotFound();
        return Ok(MapToDto(rule));
    }

    [HttpPost]
    public async Task<ActionResult<RuleDto>> CreateRule([FromBody] RuleDto dto)
    {
        var rule = MapFromDto(dto);
        var created = await _ruleService.AddRuleAsync(rule);

        await _ruleEngine.SetEnabledAsync(created.RuleId, created.IsEnabled);

        return CreatedAtAction(nameof(GetRule), new { ruleId = created.RuleId }, MapToDto(created));
    }

    [HttpPut("{ruleId}")]
    public async Task<ActionResult<RuleDto>> UpdateRule(string ruleId, [FromBody] RuleDto dto)
    {
        var rule = MapFromDto(dto);
        var updated = await _ruleService.UpdateRuleAsync(ruleId, rule);
        if (updated == null) return NotFound();

        await _ruleEngine.SetEnabledAsync(ruleId, updated.IsEnabled);

        return Ok(MapToDto(updated));
    }

    [HttpDelete("{ruleId}")]
    public async Task<ActionResult> DeleteRule(string ruleId)
    {
        var result = await _ruleService.DeleteRuleAsync(ruleId);
        if (!result) return NotFound();

        await _ruleEngine.SetEnabledAsync(ruleId, false);
        return NoContent();
    }

    [HttpPut("{ruleId}/enable")]
    public async Task<ActionResult> EnableRule(string ruleId)
    {
        var result = await _ruleService.SetEnabledAsync(ruleId, true);
        if (!result) return NotFound();

        await _ruleEngine.SetEnabledAsync(ruleId, true);
        return Ok();
    }

    [HttpPut("{ruleId}/disable")]
    public async Task<ActionResult> DisableRule(string ruleId)
    {
        var result = await _ruleService.SetEnabledAsync(ruleId, false);
        if (!result) return NotFound();

        await _ruleEngine.SetEnabledAsync(ruleId, false);
        return Ok();
    }

    [HttpPost("debug")]
    public async Task<ActionResult<RuleDebugResponseDto>> DebugRule([FromBody] RuleDebugDto dto)
    {
        try
        {
            var request = new RuleDebugRequest
            {
                RuleName = dto.RuleName,
                Script = dto.Script,
                SourcePointId = dto.SourcePointId,
                TestValue = dto.TestValue,
                Inputs = dto.Inputs
            };

            var result = await _ruleEngine.DebugRuleAsync(request);

            return Ok(new RuleDebugResponseDto
            {
                Success = result.Success,
                Result = result.Result,
                ExecutionMs = result.ExecutionMs,
                ErrorMessage = result.ErrorMessage,
                Variables = result.Variables
            });
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Debug rule failed");
            return Ok(new RuleDebugResponseDto
            {
                Success = false,
                ErrorMessage = ex.Message
            });
        }
    }

    [HttpGet("performance")]
    public async Task<ActionResult<List<RulePerformanceDto>>> GetPerformanceStats()
    {
        var stats = await _ruleEngine.GetAllPerformanceStatsAsync();
        var dtos = stats.Select(s => new RulePerformanceDto
        {
            RuleId = s.RuleId,
            RuleName = s.RuleName,
            TotalExecutions = s.TotalExecutions,
            SuccessCount = s.SuccessCount,
            FailureCount = s.FailureCount,
            AverageExecutionMs = Math.Round(s.AverageExecutionMs, 3),
            MaxExecutionMs = Math.Round(s.MaxExecutionMs, 3),
            MinExecutionMs = Math.Round(s.MinExecutionMs, 3),
            P95ExecutionMs = Math.Round(s.P95ExecutionMs, 3),
            P99ExecutionMs = Math.Round(s.P99ExecutionMs, 3),
            AutoDisabledCount = s.AutoDisabledCount,
            LastExecution = s.LastExecution,
            LastExecutionMs = Math.Round(s.LastExecutionMs, 3),
            CurrentStatus = s.CurrentStatus.ToString()
        }).ToList();

        return Ok(dtos);
    }

    [HttpGet("{ruleId}/performance")]
    public async Task<ActionResult<RulePerformanceDto>> GetRulePerformance(string ruleId)
    {
        var stats = await _ruleEngine.GetPerformanceStatsAsync(ruleId);
        var dto = new RulePerformanceDto
        {
            RuleId = stats.RuleId,
            RuleName = stats.RuleName,
            TotalExecutions = stats.TotalExecutions,
            SuccessCount = stats.SuccessCount,
            FailureCount = stats.FailureCount,
            AverageExecutionMs = Math.Round(stats.AverageExecutionMs, 3),
            MaxExecutionMs = Math.Round(stats.MaxExecutionMs, 3),
            MinExecutionMs = Math.Round(stats.MinExecutionMs, 3),
            P95ExecutionMs = Math.Round(stats.P95ExecutionMs, 3),
            P99ExecutionMs = Math.Round(stats.P99ExecutionMs, 3),
            AutoDisabledCount = stats.AutoDisabledCount,
            LastExecution = stats.LastExecution,
            LastExecutionMs = Math.Round(stats.LastExecutionMs, 3),
            CurrentStatus = stats.CurrentStatus.ToString()
        };

        return Ok(dto);
    }

    [HttpGet("virtual/{deviceId}")]
    public async Task<ActionResult<List<VirtualPointDto>>> GetVirtualPoints(string deviceId)
    {
        var points = await _ruleService.GetVirtualPointsAsync(deviceId);
        var dtos = points.Select(p => new VirtualPointDto
        {
            PointId = p.PointId,
            DeviceId = p.DeviceId,
            RuleId = p.RuleId,
            PointName = p.PointName,
            Unit = p.Unit,
            Value = p.Value,
            Timestamp = p.Timestamp,
            Quality = p.Quality
        }).ToList();

        return Ok(dtos);
    }

    private static RuleDto MapToDto(CalculationRule rule)
    {
        return new RuleDto
        {
            RuleId = rule.RuleId,
            RuleName = rule.RuleName,
            DeviceId = rule.DeviceId,
            RuleType = rule.RuleType.ToString(),
            SourcePointId = rule.SourcePointId,
            OutputPointName = rule.OutputPointName,
            OutputUnit = rule.OutputUnit,
            Script = rule.Script,
            Description = rule.Description,
            IsEnabled = rule.IsEnabled,
            Status = rule.Status.ToString(),
            WindowSize = rule.WindowSize,
            MaxExecutionTimeMs = rule.MaxExecutionTimeMs,
            MaxFailCount = rule.MaxFailCount
        };
    }

    private static CalculationRule MapFromDto(RuleDto dto)
    {
        return new CalculationRule
        {
            RuleId = dto.RuleId,
            RuleName = dto.RuleName,
            DeviceId = dto.DeviceId,
            RuleType = Enum.TryParse<RuleType>(dto.RuleType, out var type) ? type : RuleType.CustomScript,
            SourcePointId = dto.SourcePointId,
            OutputPointName = dto.OutputPointName,
            OutputUnit = dto.OutputUnit,
            Script = dto.Script,
            Description = dto.Description,
            IsEnabled = dto.IsEnabled,
            WindowSize = dto.WindowSize,
            MaxExecutionTimeMs = dto.MaxExecutionTimeMs,
            MaxFailCount = dto.MaxFailCount
        };
    }
}