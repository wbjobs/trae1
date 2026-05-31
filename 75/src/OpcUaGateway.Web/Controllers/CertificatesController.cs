using Microsoft.AspNetCore.Mvc;
using OpcUaGateway.Core.Configuration;
using OpcUaGateway.Core.DTOs;
using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using Microsoft.Extensions.Options;

namespace OpcUaGateway.Web.Controllers;

[ApiController]
[Route("api/[controller]")]
public class CertificatesController : ControllerBase
{
    private readonly ICertificateManager _certificateManager;
    private readonly GatewayConfig _config;
    private readonly IDataCollectionService _dataCollectionService;

    public CertificatesController(
        ICertificateManager certificateManager,
        IOptions<GatewayConfig> config,
        IDataCollectionService dataCollectionService)
    {
        _certificateManager = certificateManager;
        _config = config.Value;
        _dataCollectionService = dataCollectionService;
    }

    [HttpGet("config")]
    public ActionResult<CertificateConfigDto> GetConfig()
    {
        return Ok(new CertificateConfigDto
        {
            AcceptanceStrategy = _config.Certificate.AcceptanceStrategy.ToString(),
            ExpiryWarningDays = _config.Certificate.ExpiryWarningDays,
            AutoRenegotiateOnChange = _config.Certificate.AutoRenegotiateOnChange,
            AuditLogRetentionDays = _config.Certificate.AuditLogRetentionDays
        });
    }

    [HttpGet("strategies")]
    public ActionResult<List<string>> GetStrategies()
    {
        var strategies = Enum.GetNames(typeof(CertificateAcceptanceStrategy)).ToList();
        return Ok(strategies);
    }

    [HttpPut("strategy")]
    public IActionResult UpdateStrategy([FromBody] CertificateConfigDto dto)
    {
        if (Enum.TryParse<CertificateAcceptanceStrategy>(dto.AcceptanceStrategy, true, out var strategy))
        {
            _certificateManager.Strategy = strategy;
            return Ok();
        }
        return BadRequest("Invalid strategy");
    }

    [HttpGet("whitelist")]
    public async Task<ActionResult<List<CertificateWhitelistDto>>> GetWhitelist([FromQuery] string? deviceId = null)
    {
        var entries = await _certificateManager.GetWhitelistAsync(deviceId);
        var dtos = entries.Select(e => new CertificateWhitelistDto
        {
            Id = e.Id,
            Thumbprint = e.Thumbprint,
            Subject = e.Subject,
            Issuer = e.Issuer,
            NotAfter = e.NotAfter,
            DeviceId = e.DeviceId,
            Description = e.Description,
            AddedAt = e.AddedAt,
            AddedBy = e.AddedBy,
            IsEnabled = e.IsEnabled
        }).ToList();
        return Ok(dtos);
    }

    [HttpPost("whitelist")]
    public async Task<ActionResult<CertificateWhitelistDto>> AddToWhitelist([FromBody] WhitelistAddDto dto)
    {
        var entry = new CertificateWhitelistEntry
        {
            Thumbprint = dto.Thumbprint,
            Subject = dto.Subject,
            Issuer = dto.Issuer,
            NotAfter = dto.NotAfter,
            DeviceId = dto.DeviceId,
            Description = dto.Description,
            AddedAt = DateTime.UtcNow,
            AddedBy = "WebUI",
            IsEnabled = true
        };

        var result = await _certificateManager.AddToWhitelistAsync(entry);
        return Ok(new CertificateWhitelistDto
        {
            Id = result.Id,
            Thumbprint = result.Thumbprint,
            Subject = result.Subject,
            Issuer = result.Issuer,
            NotAfter = result.NotAfter,
            DeviceId = result.DeviceId,
            Description = result.Description,
            AddedAt = result.AddedAt,
            AddedBy = result.AddedBy,
            IsEnabled = result.IsEnabled
        });
    }

    [HttpDelete("whitelist/{id}")]
    public async Task<IActionResult> RemoveFromWhitelist(long id)
    {
        var result = await _certificateManager.RemoveFromWhitelistAsync(id);
        if (result) return NoContent();
        return NotFound();
    }

    [HttpGet("audit")]
    public async Task<ActionResult<List<CertificateAuditLogDto>>> GetAuditLogs(
        [FromQuery] int hours = 24,
        [FromQuery] string? deviceId = null)
    {
        var logs = await _certificateManager.GetAuditLogsAsync(hours, deviceId);
        var dtos = logs.Select(l => new CertificateAuditLogDto
        {
            Id = l.Id,
            DeviceId = l.DeviceId,
            DeviceName = l.DeviceName,
            EventType = l.EventType.ToString(),
            Thumbprint = l.Thumbprint,
            Subject = l.Subject,
            PreviousThumbprint = l.PreviousThumbprint,
            EventTime = l.EventTime,
            Details = l.Details,
            StrategyUsed = l.StrategyUsed.ToString(),
            IsAccepted = l.IsAccepted,
            Operator = l.Operator
        }).ToList();
        return Ok(dtos);
    }

    [HttpGet("current/{deviceId}")]
    public async Task<ActionResult<CertificateInfoDto>> GetCurrentCertificate(string deviceId)
    {
        var statuses = await _dataCollectionService.GetDeviceStatusesAsync();
        var status = statuses.FirstOrDefault(s => s.DeviceId == deviceId);
        if (status == null) return NotFound();

        var log = await _certificateManager.GetLatestCertificateInfoAsync(deviceId);
        var dto = new CertificateInfoDto
        {
            DeviceId = deviceId,
            DeviceName = status.DeviceName,
            Thumbprint = log?.Thumbprint ?? "N/A",
            Subject = log?.Subject ?? "N/A",
            Issuer = string.Empty,
            NotBefore = DateTime.MinValue,
            NotAfter = log?.EventTime ?? DateTime.MinValue,
            SerialNumber = string.Empty,
            SignatureAlgorithm = string.Empty,
            IsSelfSigned = false,
            IsExpired = false,
            DaysUntilExpiry = 0
        };

        return Ok(dto);
    }
}