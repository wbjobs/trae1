using IndustrialDataCollector.Core.Entities;

namespace IndustrialDataCollector.Core.Interfaces;

public interface IDataStorage
{
    Task WriteDataAsync(IEnumerable<DataPoint> dataPoints);
    Task<IEnumerable<DataPoint>> QueryDataAsync(
        string measurement,
        DateTime startTime,
        DateTime endTime,
        string? deviceName = null,
        string? registerName = null);
    Task<IEnumerable<DataPoint>> GetLatestDataAsync(
        string measurement,
        string? deviceName = null,
        int limit = 100);
}
