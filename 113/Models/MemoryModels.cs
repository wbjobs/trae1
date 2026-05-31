namespace MemoryDiagnostics.Models;

public record PhysicalMemoryDistribution
{
    public ulong TotalPhysical { get; set; }
    public ulong ProcessPrivate { get; set; }
    public ulong SharedMemory { get; set; }
    public ulong PagedPool { get; set; }
    public ulong NonPagedPool { get; set; }
    public ulong SystemCache { get; set; }
    public ulong DriverLocked { get; set; }
    public ulong FreeMemory { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.Now;
}

public record ProcessMemoryInfo
{
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = string.Empty;
    public long PrivateWorkingSet { get; set; }
    public long SharedWorkingSet { get; set; }
    public long WorkingSetSize { get; set; }
    public long PrivateBytes { get; set; }
    public long PageFileUsage { get; set; }
    public long PagedPoolUsage { get; set; }
    public long NonPagedPoolUsage { get; set; }
    public uint HandleCount { get; set; }
    public uint ThreadCount { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.Now;
}

public record PoolTagEntry
{
    public string Tag { get; set; } = string.Empty;
    public string TagName { get; set; } = string.Empty;
    public long PagedUsed { get; set; }
    public long NonPagedUsed { get; set; }
    public long PagedAllocs { get; set; }
    public long PagedFrees { get; set; }
    public long NonPagedAllocs { get; set; }
    public long NonPagedFrees { get; set; }
    public DateTime Timestamp { get; set; } = DateTime.Now;
}

public record MemorySample
{
    public DateTime Timestamp { get; set; }
    public PhysicalMemoryDistribution SystemMemory { get; set; } = new();
    public List<ProcessMemoryInfo> Processes { get; set; } = new();
    public List<PoolTagEntry> PoolTags { get; set; } = new();
}

public record ProcessMemoryTrend
{
    public int ProcessId { get; set; }
    public string ProcessName { get; set; } = string.Empty;
    public List<(DateTime Time, long PrivateWorkingSet)> Samples { get; set; } = new();
    public double GrowthRateBytesPerHour { get; set; }
    public bool IsSuspectedLeak { get; set; }
    public string LeakSeverity { get; set; } = "None";
    public long InitialValue { get; set; }
    public long CurrentValue { get; set; }
    public long MaxValue { get; set; }
    public double GrowthPercentage { get; set; }
}

public record LeakReport
{
    public DateTime StartTime { get; set; }
    public DateTime EndTime { get; set; }
    public TimeSpan Duration { get; set; }
    public List<ProcessMemoryTrend> SuspectedProcesses { get; set; } = new();
    public PhysicalMemoryDistribution StartMemory { get; set; } = new();
    public PhysicalMemoryDistribution EndMemory { get; set; } = new();
    public string Notes { get; set; } = string.Empty;
}

public class EtwMemoryEvent
{
    public DateTime Timestamp { get; set; }
    public int ProcessId { get; set; }
    public string EventType { get; set; } = string.Empty;
    public long AllocationSize { get; set; }
    public ulong Address { get; set; }
    public string PoolTag { get; set; } = string.Empty;
}

public static class MemoryFormatter
{
    public static string FormatBytes(long bytes)
    {
        string[] suffixes = { "B", "KB", "MB", "GB", "TB", "PB" };
        int counter = 0;
        decimal number = bytes;
        while (Math.Round(number / 1024) >= 1)
        {
            number /= 1024;
            counter++;
        }
        return $"{number:0.##} {suffixes[counter]}";
    }

    public static string FormatBytes(ulong bytes)
    {
        return FormatBytes((long)bytes);
    }
}
