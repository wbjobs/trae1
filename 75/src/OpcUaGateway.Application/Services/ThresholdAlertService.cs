using OpcUaGateway.Core.Interfaces;
using OpcUaGateway.Core.Models;
using Microsoft.Extensions.Logging;

namespace OpcUaGateway.Application.Services;

public interface IThresholdAlertService
{
    List<ThresholdAlert> CheckAlerts(string deviceId, List<DataPointReadResult> results, Dictionary<string, DataPoint> pointMap);
    List<ThresholdAlert> GetActiveAlerts();
    List<ThresholdAlert> GetAlertHistory(int hours = 24);
    Task AcknowledgeAlertAsync(string alertId);
}

public class ThresholdAlertService : IThresholdAlertService
{
    private readonly ILogger<ThresholdAlertService> _logger;
    private readonly List<ThresholdAlert> _activeAlerts = new();
    private readonly List<ThresholdAlert> _alertHistory = new();
    private readonly object _lock = new();
    private readonly Dictionary<string, DateTime> _alertCooldown = new();

    public ThresholdAlertService(ILogger<ThresholdAlertService> logger)
    {
        _logger = logger;
    }

    public List<ThresholdAlert> CheckAlerts(string deviceId, List<DataPointReadResult> results, Dictionary<string, DataPoint> pointMap)
    {
        var triggeredAlerts = new List<ThresholdAlert>();

        foreach (var result in results)
        {
            if (!pointMap.TryGetValue(result.PointId, out var dataPoint))
                continue;

            var alert = CheckSinglePoint(deviceId, result, dataPoint);
            if (alert != null)
            {
                triggeredAlerts.Add(alert);
            }
        }

        if (triggeredAlerts.Count > 0)
        {
            lock (_lock)
            {
                _activeAlerts.AddRange(triggeredAlerts);
                _alertHistory.AddRange(triggeredAlerts);
            }

            _logger.LogWarning("Triggered {Count} threshold alerts for device {DeviceId}", triggeredAlerts.Count, deviceId);
        }

        return triggeredAlerts;
    }

    private ThresholdAlert? CheckSinglePoint(string deviceId, DataPointReadResult result, DataPoint dataPoint)
    {
        var cooldownKey = $"{deviceId}_{result.PointId}";

        lock (_lock)
        {
            if (_alertCooldown.TryGetValue(cooldownKey, out var lastAlertTime))
            {
                if ((DateTime.UtcNow - lastAlertTime).TotalSeconds < 30)
                {
                    return null;
                }
            }
        }

        if (dataPoint.UpperThreshold.HasValue && result.Value > dataPoint.UpperThreshold.Value)
        {
            lock (_lock)
            {
                _alertCooldown[cooldownKey] = DateTime.UtcNow;
            }

            return new ThresholdAlert
            {
                AlertId = Guid.NewGuid().ToString(),
                DeviceId = deviceId,
                PointId = result.PointId,
                PointName = result.PointName,
                AlertType = AlertType.UpperLimit,
                ThresholdValue = dataPoint.UpperThreshold.Value,
                ActualValue = result.Value,
                TriggerTime = DateTime.UtcNow,
                IsAcknowledged = false,
                Message = $"Upper threshold exceeded: {result.PointName} = {result.Value:F2} (limit: {dataPoint.UpperThreshold.Value:F2})"
            };
        }

        if (dataPoint.LowerThreshold.HasValue && result.Value < dataPoint.LowerThreshold.Value)
        {
            lock (_lock)
            {
                _alertCooldown[cooldownKey] = DateTime.UtcNow;
            }

            return new ThresholdAlert
            {
                AlertId = Guid.NewGuid().ToString(),
                DeviceId = deviceId,
                PointId = result.PointId,
                PointName = result.PointName,
                AlertType = AlertType.LowerLimit,
                ThresholdValue = dataPoint.LowerThreshold.Value,
                ActualValue = result.Value,
                TriggerTime = DateTime.UtcNow,
                IsAcknowledged = false,
                Message = $"Lower threshold exceeded: {result.PointName} = {result.Value:F2} (limit: {dataPoint.LowerThreshold.Value:F2})"
            };
        }

        return null;
    }

    public List<ThresholdAlert> GetActiveAlerts()
    {
        lock (_lock)
        {
            return _activeAlerts.Where(a => !a.IsAcknowledged).ToList();
        }
    }

    public List<ThresholdAlert> GetAlertHistory(int hours = 24)
    {
        lock (_lock)
        {
            var cutoff = DateTime.UtcNow.AddHours(-hours);
            return _alertHistory.Where(a => a.TriggerTime >= cutoff).OrderByDescending(a => a.TriggerTime).ToList();
        }
    }

    public Task AcknowledgeAlertAsync(string alertId)
    {
        lock (_lock)
        {
            var alert = _activeAlerts.FirstOrDefault(a => a.AlertId == alertId);
            if (alert != null)
            {
                alert.IsAcknowledged = true;
                _logger.LogInformation("Alert {AlertId} acknowledged", alertId);
            }
        }
        return Task.CompletedTask;
    }
}