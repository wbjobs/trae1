using IndustrialDataCollector.Core.Entities;
using IndustrialDataCollector.Core.Interfaces;
using InfluxDB.Client;
using InfluxDB.Client.Api.Domain;
using InfluxDB.Client.Core.Flux.Domain;
using InfluxDB.Client.Writes;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace IndustrialDataCollector.Infrastructure.Services;

public class InfluxDbSettings
{
    public string Url { get; set; } = "http://localhost:8086";
    public string Token { get; set; } = string.Empty;
    public string Organization { get; set; } = "industrial";
    public string Bucket { get; set; } = "device_data";
}

public class InfluxDbStorage : IDataStorage
{
    private readonly InfluxDBClient _client;
    private readonly InfluxDbSettings _settings;
    private readonly ILogger<InfluxDbStorage> _logger;

    public InfluxDbStorage(IOptions<InfluxDbSettings> options, ILogger<InfluxDbStorage> logger)
    {
        _settings = options.Value;
        _logger = logger;
        _client = new InfluxDBClient(_settings.Url, _settings.Token);
    }

    public async Task WriteDataAsync(IEnumerable<DataPoint> dataPoints)
    {
        try
        {
            using var writeApi = _client.GetWriteApi();

            foreach (var dataPoint in dataPoints)
            {
                var point = PointData.Measurement(dataPoint.Measurement)
                    .Tag("device_id", dataPoint.DeviceId.ToString())
                    .Tag("device_name", dataPoint.DeviceName)
                    .Tag("register_name", dataPoint.RegisterName)
                    .Field("value", dataPoint.Value)
                    .Timestamp(dataPoint.Timestamp, WritePrecision.Ns);

                if (!string.IsNullOrEmpty(dataPoint.Unit))
                {
                    point = point.Tag("unit", dataPoint.Unit);
                }

                writeApi.WritePoint(point, _settings.Bucket, _settings.Organization);
            }

            _logger.LogDebug("Written {Count} data points to InfluxDB", dataPoints.Count());
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error writing data to InfluxDB");
            throw;
        }
    }

    public async Task<IEnumerable<DataPoint>> QueryDataAsync(
        string measurement,
        DateTime startTime,
        DateTime endTime,
        string? deviceName = null,
        string? registerName = null)
    {
        try
        {
            var flux = $"from(bucket: \"{_settings.Bucket}\") " +
                       $"|> range(start: {startTime:o}, stop: {endTime:o}) " +
                       $"|> filter(fn: (r) => r._measurement == \"{measurement}\")";

            if (!string.IsNullOrEmpty(deviceName))
            {
                flux += $" |> filter(fn: (r) => r.device_name == \"{deviceName}\")";
            }

            if (!string.IsNullOrEmpty(registerName))
            {
                flux += $" |> filter(fn: (r) => r.register_name == \"{registerName}\")";
            }

            flux += " |> keep(columns: [\"_time\", \"_value\", \"device_id\", \"device_name\", \"register_name\", \"unit\"])";

            var queryApi = _client.GetQueryApi();
            var tables = await queryApi.QueryAsync(flux, _settings.Organization);

            return FluxToDataPoints(tables, measurement);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error querying data from InfluxDB");
            return Enumerable.Empty<DataPoint>();
        }
    }

    public async Task<IEnumerable<DataPoint>> GetLatestDataAsync(
        string measurement,
        string? deviceName = null,
        int limit = 100)
    {
        try
        {
            var flux = $"from(bucket: \"{_settings.Bucket}\") " +
                       $"|> range(start: -{limit}s) " +
                       $"|> filter(fn: (r) => r._measurement == \"{measurement}\")";

            if (!string.IsNullOrEmpty(deviceName))
            {
                flux += $" |> filter(fn: (r) => r.device_name == \"{deviceName}\")";
            }

            flux += $" |> limit(n: {limit})";

            var queryApi = _client.GetQueryApi();
            var tables = await queryApi.QueryAsync(flux, _settings.Organization);

            return FluxToDataPoints(tables, measurement);
        }
        catch (Exception ex)
        {
            _logger.LogError(ex, "Error getting latest data from InfluxDB");
            return Enumerable.Empty<DataPoint>();
        }
    }

    private List<DataPoint> FluxToDataPoints(List<FluxTable> tables, string measurement)
    {
        var dataPoints = new List<DataPoint>();

        foreach (var table in tables)
        {
            foreach (var record in table.Records)
            {
                var dataPoint = new DataPoint
                {
                    Measurement = measurement,
                    Value = Convert.ToDouble(record.GetValue()),
                    Timestamp = record.GetTime()?.ToDateTimeUtc() ?? DateTime.UtcNow
                };

                if (record.Values.TryGetValue("device_id", out var deviceId))
                {
                    dataPoint.DeviceId = Convert.ToInt32(deviceId);
                }

                if (record.Values.TryGetValue("device_name", out var deviceName))
                {
                    dataPoint.DeviceName = deviceName?.ToString() ?? string.Empty;
                }

                if (record.Values.TryGetValue("register_name", out var registerName))
                {
                    dataPoint.RegisterName = registerName?.ToString() ?? string.Empty;
                }

                if (record.Values.TryGetValue("unit", out var unit))
                {
                    dataPoint.Unit = unit?.ToString();
                }

                dataPoints.Add(dataPoint);
            }
        }

        return dataPoints;
    }
}
