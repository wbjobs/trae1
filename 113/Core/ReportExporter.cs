using System.Globalization;
using System.IO;
using System.Text;
using System.Text.Json;
using CsvHelper;
using CsvHelper.Configuration;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class ReportExporter
{
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase
    };

    public async Task ExportMemorySampleAsync(MemorySample sample, string filePath, ExportFormat format)
    {
        switch (format)
        {
            case ExportFormat.Json:
                await ExportToJsonAsync(sample, filePath);
                break;
            case ExportFormat.Csv:
                await ExportToCsvAsync(sample, filePath);
                break;
            default:
                throw new ArgumentException($"不支持的导出格式: {format}");
        }
    }

    public async Task ExportLeakReportAsync(LeakReport report, string filePath, ExportFormat format)
    {
        switch (format)
        {
            case ExportFormat.Json:
                await ExportToJsonAsync(report, filePath);
                break;
            case ExportFormat.Csv:
                await ExportLeakReportToCsvAsync(report, filePath);
                break;
            default:
                throw new ArgumentException($"不支持的导出格式: {format}");
        }
    }

    public async Task ExportProcessListAsync(List<ProcessMemoryInfo> processes, string filePath, ExportFormat format)
    {
        switch (format)
        {
            case ExportFormat.Json:
                await ExportToJsonAsync(processes, filePath);
                break;
            case ExportFormat.Csv:
                await ExportProcessesToCsvAsync(processes, filePath);
                break;
            default:
                throw new ArgumentException($"不支持的导出格式: {format}");
        }
    }

    public async Task ExportPoolTagsAsync(List<PoolTagEntry> poolTags, string filePath, ExportFormat format)
    {
        switch (format)
        {
            case ExportFormat.Json:
                await ExportToJsonAsync(poolTags, filePath);
                break;
            case ExportFormat.Csv:
                await ExportPoolTagsToCsvAsync(poolTags, filePath);
                break;
            default:
                throw new ArgumentException($"不支持的导出格式: {format}");
        }
    }

    private async Task ExportToJsonAsync<T>(T data, string filePath)
    {
        var json = JsonSerializer.Serialize(data, _jsonOptions);
        await File.WriteAllTextAsync(filePath, json, Encoding.UTF8);
    }

    private async Task ExportToCsvAsync(MemorySample sample, string filePath)
    {
        var directory = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var baseName = Path.GetFileNameWithoutExtension(filePath);
        var ext = Path.GetExtension(filePath);

        await using (var writer = new StreamWriter(Path.Combine(directory!, $"{baseName}_SystemMemory{ext}")))
        await using (var csv = new CsvWriter(writer, new CsvConfiguration(CultureInfo.InvariantCulture)))
        {
            csv.WriteHeader<CsvMemoryDistribution>();
            await csv.NextRecordAsync();
            csv.WriteRecord(new CsvMemoryDistribution
            {
                Timestamp = sample.Timestamp.ToString("yyyy-MM-dd HH:mm:ss"),
                TotalPhysical = sample.SystemMemory.TotalPhysical,
                ProcessPrivate = sample.SystemMemory.ProcessPrivate,
                SharedMemory = sample.SystemMemory.SharedMemory,
                PagedPool = sample.SystemMemory.PagedPool,
                NonPagedPool = sample.SystemMemory.NonPagedPool,
                SystemCache = sample.SystemMemory.SystemCache,
                DriverLocked = sample.SystemMemory.DriverLocked,
                FreeMemory = sample.SystemMemory.FreeMemory,
                TotalPhysicalFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.TotalPhysical),
                ProcessPrivateFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.ProcessPrivate),
                SharedMemoryFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.SharedMemory),
                PagedPoolFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.PagedPool),
                NonPagedPoolFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.NonPagedPool),
                SystemCacheFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.SystemCache),
                DriverLockedFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.DriverLocked),
                FreeMemoryFormatted = MemoryFormatter.FormatBytes(sample.SystemMemory.FreeMemory)
            });
            await csv.FlushAsync();
        }

        await ExportProcessesToCsvAsync(sample.Processes, Path.Combine(directory!, $"{baseName}_Processes{ext}"));
        await ExportPoolTagsToCsvAsync(sample.PoolTags, Path.Combine(directory!, $"{baseName}_PoolTags{ext}"));
    }

    private async Task ExportProcessesToCsvAsync(List<ProcessMemoryInfo> processes, string filePath)
    {
        var directory = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        await using var writer = new StreamWriter(filePath);
        await using var csv = new CsvWriter(writer, new CsvConfiguration(CultureInfo.InvariantCulture));
        csv.WriteHeader<CsvProcessInfo>();
        await csv.NextRecordAsync();

        foreach (var proc in processes)
        {
            csv.WriteRecord(new CsvProcessInfo
            {
                ProcessId = proc.ProcessId,
                ProcessName = proc.ProcessName,
                WorkingSetSize = proc.WorkingSetSize,
                PrivateWorkingSet = proc.PrivateWorkingSet,
                SharedWorkingSet = proc.SharedWorkingSet,
                PrivateBytes = proc.PrivateBytes,
                PageFileUsage = proc.PageFileUsage,
                PagedPoolUsage = proc.PagedPoolUsage,
                NonPagedPoolUsage = proc.NonPagedPoolUsage,
                WorkingSetSizeFormatted = MemoryFormatter.FormatBytes(proc.WorkingSetSize),
                PrivateWorkingSetFormatted = MemoryFormatter.FormatBytes(proc.PrivateWorkingSet),
                SharedWorkingSetFormatted = MemoryFormatter.FormatBytes(proc.SharedWorkingSet),
                PrivateBytesFormatted = MemoryFormatter.FormatBytes(proc.PrivateBytes),
                Timestamp = proc.Timestamp.ToString("yyyy-MM-dd HH:mm:ss")
            });
            await csv.NextRecordAsync();
        }

        await csv.FlushAsync();
    }

    private async Task ExportPoolTagsToCsvAsync(List<PoolTagEntry> poolTags, string filePath)
    {
        var directory = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        await using var writer = new StreamWriter(filePath);
        await using var csv = new CsvWriter(writer, new CsvConfiguration(CultureInfo.InvariantCulture));
        csv.WriteHeader<CsvPoolTag>();
        await csv.NextRecordAsync();

        foreach (var tag in poolTags)
        {
            csv.WriteRecord(new CsvPoolTag
            {
                Tag = tag.Tag,
                TagName = tag.TagName,
                PagedUsed = tag.PagedUsed,
                NonPagedUsed = tag.NonPagedUsed,
                TotalUsed = tag.PagedUsed + tag.NonPagedUsed,
                PagedAllocs = tag.PagedAllocs,
                PagedFrees = tag.PagedFrees,
                NonPagedAllocs = tag.NonPagedAllocs,
                NonPagedFrees = tag.NonPagedFrees,
                PagedUsedFormatted = MemoryFormatter.FormatBytes(tag.PagedUsed),
                NonPagedUsedFormatted = MemoryFormatter.FormatBytes(tag.NonPagedUsed),
                TotalUsedFormatted = MemoryFormatter.FormatBytes(tag.PagedUsed + tag.NonPagedUsed),
                Timestamp = tag.Timestamp.ToString("yyyy-MM-dd HH:mm:ss")
            });
            await csv.NextRecordAsync();
        }

        await csv.FlushAsync();
    }

    private async Task ExportLeakReportToCsvAsync(LeakReport report, string filePath)
    {
        var directory = Path.GetDirectoryName(filePath);
        if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
        {
            Directory.CreateDirectory(directory);
        }

        var baseName = Path.GetFileNameWithoutExtension(filePath);
        var ext = Path.GetExtension(filePath);

        await using (var writer = new StreamWriter(Path.Combine(directory!, $"{baseName}_Summary{ext}")))
        await using (var csv = new CsvWriter(writer, new CsvConfiguration(CultureInfo.InvariantCulture)))
        {
            csv.WriteHeader<CsvLeakSummary>();
            await csv.NextRecordAsync();
            csv.WriteRecord(new CsvLeakSummary
            {
                StartTime = report.StartTime.ToString("yyyy-MM-dd HH:mm:ss"),
                EndTime = report.EndTime.ToString("yyyy-MM-dd HH:mm:ss"),
                DurationHours = report.Duration.TotalHours,
                SuspectedProcessCount = report.SuspectedProcesses.Count,
                Notes = report.Notes,
                StartPrivateBytes = (long)report.StartMemory.ProcessPrivate,
                EndPrivateBytes = (long)report.EndMemory.ProcessPrivate,
                GrowthBytes = (long)(report.EndMemory.ProcessPrivate - report.StartMemory.ProcessPrivate),
                GrowthFormatted = MemoryFormatter.FormatBytes((long)Math.Abs((long)(report.EndMemory.ProcessPrivate - report.StartMemory.ProcessPrivate)))
            });
            await csv.FlushAsync();
        }

        await using (var writer = new StreamWriter(Path.Combine(directory!, $"{baseName}_SuspectedProcesses{ext}")))
        await using (var csv = new CsvWriter(writer, new CsvConfiguration(CultureInfo.InvariantCulture)))
        {
            csv.WriteHeader<CsvSuspectedProcess>();
            await csv.NextRecordAsync();

            foreach (var proc in report.SuspectedProcesses)
            {
                csv.WriteRecord(new CsvSuspectedProcess
                {
                    ProcessId = proc.ProcessId,
                    ProcessName = proc.ProcessName,
                    GrowthRateBytesPerHour = proc.GrowthRateBytesPerHour,
                    GrowthRateFormatted = $"{MemoryFormatter.FormatBytes((long)proc.GrowthRateBytesPerHour)}/小时",
                    LeakSeverity = proc.LeakSeverity,
                    InitialValue = proc.InitialValue,
                    CurrentValue = proc.CurrentValue,
                    MaxValue = proc.MaxValue,
                    GrowthPercentage = proc.GrowthPercentage,
                    InitialValueFormatted = MemoryFormatter.FormatBytes(proc.InitialValue),
                    CurrentValueFormatted = MemoryFormatter.FormatBytes(proc.CurrentValue),
                    MaxValueFormatted = MemoryFormatter.FormatBytes(proc.MaxValue),
                    SampleCount = proc.Samples.Count
                });
                await csv.NextRecordAsync();
            }

            await csv.FlushAsync();
        }
    }

    public static ExportFormat DetectFormatFromFileName(string filePath)
    {
        var ext = Path.GetExtension(filePath).ToLower();
        return ext switch
        {
            ".json" => ExportFormat.Json,
            ".csv" => ExportFormat.Csv,
            _ => throw new ArgumentException($"无法从文件扩展名检测格式: {ext}")
        };
    }

    public async Task ExportToJsonAsync<T>(T data, string filePath)
    {
        var json = JsonSerializer.Serialize(data, _jsonOptions);
        await File.WriteAllTextAsync(filePath, json, Encoding.UTF8);
    }
}

public enum ExportFormat
{
    Json,
    Csv
}

public class CsvMemoryDistribution
{
    public string Timestamp { get; set; } = string.Empty;
    public ulong TotalPhysical { get; set; }
    public ulong ProcessPrivate { get; set; }
    public ulong SharedMemory { get; set; }
    public ulong PagedPool { get; set; }
    public ulong NonPagedPool { get; set; }
    public ulong SystemCache { get; set; }
    public ulong DriverLocked { get; set; }
    public ulong FreeMemory { get; set; }
    public string TotalPhysicalFormatted { get; set; } = string.Empty;
    public string ProcessPrivateFormatted { get; set; } = string.Empty;
    public string SharedMemoryFormatted { get; set; } = string.Empty;
    public string PagedPoolFormatted { get; set; } = string.Empty;
    public string NonPagedPoolFormatted { get; set; } = string.Empty;
    public string SystemCacheFormatted { get; set; } = string.Empty;
    public string DriverLockedFormatted { get; set; } = string.Empty;
    public string FreeMemoryFormatted { get; set; } = string.Empty;
}

public class CsvProcessInfo
{
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = string.Empty;
    public long WorkingSetSize { get; set; }
    public long PrivateWorkingSet { get; set; }
    public long SharedWorkingSet { get; set; }
    public long PrivateBytes { get; set; }
    public long PageFileUsage { get; set; }
    public long PagedPoolUsage { get; set; }
    public long NonPagedPoolUsage { get; set; }
    public string WorkingSetSizeFormatted { get; set; } = string.Empty;
    public string PrivateWorkingSetFormatted { get; set; } = string.Empty;
    public string SharedWorkingSetFormatted { get; set; } = string.Empty;
    public string PrivateBytesFormatted { get; set; } = string.Empty;
    public string Timestamp { get; set; } = string.Empty;
}

public class CsvPoolTag
{
    public string Tag { get; set; } = string.Empty;
    public string TagName { get; set; } = string.Empty;
    public long PagedUsed { get; set; }
    public long NonPagedUsed { get; set; }
    public long TotalUsed { get; set; }
    public long PagedAllocs { get; set; }
    public long PagedFrees { get; set; }
    public long NonPagedAllocs { get; set; }
    public long NonPagedFrees { get; set; }
    public string PagedUsedFormatted { get; set; } = string.Empty;
    public string NonPagedUsedFormatted { get; set; } = string.Empty;
    public string TotalUsedFormatted { get; set; } = string.Empty;
    public string Timestamp { get; set; } = string.Empty;
}

public class CsvLeakSummary
{
    public string StartTime { get; set; } = string.Empty;
    public string EndTime { get; set; } = string.Empty;
    public double DurationHours { get; set; }
    public int SuspectedProcessCount { get; set; }
    public string Notes { get; set; } = string.Empty;
    public long StartPrivateBytes { get; set; }
    public long EndPrivateBytes { get; set; }
    public long GrowthBytes { get; set; }
    public string GrowthFormatted { get; set; } = string.Empty;
}

public class CsvSuspectedProcess
{
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = string.Empty;
    public double GrowthRateBytesPerHour { get; set; }
    public string GrowthRateFormatted { get; set; } = string.Empty;
    public string LeakSeverity { get; set; } = string.Empty;
    public long InitialValue { get; set; }
    public long CurrentValue { get; set; }
    public long MaxValue { get; set; }
    public double GrowthPercentage { get; set; }
    public string InitialValueFormatted { get; set; } = string.Empty;
    public string CurrentValueFormatted { get; set; } = string.Empty;
    public string MaxValueFormatted { get; set; } = string.Empty;
    public int SampleCount { get; set; }
}
