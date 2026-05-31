using System.Collections.Concurrent;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class LeakDetector
{
    private readonly ConcurrentDictionary<int, ProcessMemoryTrend> _processTrends = new();
    private readonly MemoryAnalyzer _analyzer;
    private DateTime _startTime;
    private PhysicalMemoryDistribution? _startMemory;
    private readonly int _minSamplesForAnalysis = 30;
    private readonly long _leakThresholdBytesPerHour = 1024 * 1024 * 5;
    private readonly double _leakGrowthPercentageThreshold = 20.0;

    public bool FastMode
    {
        get => _analyzer.FastMode;
        set => _analyzer.FastMode = value;
    }

    public LeakDetector(bool fastMode = false)
    {
        _analyzer = new MemoryAnalyzer { FastMode = fastMode };
        _startTime = DateTime.Now;
    }

    public void StartMonitoring()
    {
        _startTime = DateTime.Now;
        _startMemory = _analyzer.GetPhysicalMemoryDistribution();
        _processTrends.Clear();
    }

    public void AddSample(MemorySample sample)
    {
        foreach (var proc in sample.Processes)
        {
            var trend = _processTrends.GetOrAdd(proc.ProcessId, _ => new ProcessMemoryTrend
            {
                ProcessId = proc.ProcessId,
                ProcessName = proc.ProcessName,
                InitialValue = proc.PrivateWorkingSet,
                CurrentValue = proc.PrivateWorkingSet,
                MaxValue = proc.PrivateWorkingSet
            });

            trend.ProcessName = proc.ProcessName;
            trend.CurrentValue = proc.PrivateWorkingSet;
            if (proc.PrivateWorkingSet > trend.MaxValue)
            {
                trend.MaxValue = proc.PrivateWorkingSet;
            }

            trend.Samples.Add((sample.Timestamp, proc.PrivateWorkingSet));

            if (trend.Samples.Count > _minSamplesForAnalysis)
            {
                AnalyzeTrend(trend);
            }
        }

        var processIds = sample.Processes.Select(p => p.ProcessId).ToHashSet();
        var removedIds = _processTrends.Keys.Where(id => !processIds.Contains(id)).ToList();
        foreach (var id in removedIds)
        {
            _processTrends.TryRemove(id, out _);
        }
    }

    private void AnalyzeTrend(ProcessMemoryTrend trend)
    {
        if (trend.Samples.Count < 2)
        {
            return;
        }

        var samples = trend.Samples.TakeLast(_minSamplesForAnalysis).ToList();
        if (samples.Count < 2)
        {
            return;
        }

        double growthRate = CalculateGrowthRate(samples);
        trend.GrowthRateBytesPerHour = growthRate;

        if (trend.InitialValue > 0)
        {
            trend.GrowthPercentage = ((double)(trend.CurrentValue - trend.InitialValue) / trend.InitialValue) * 100;
        }

        bool isGrowing = growthRate > _leakThresholdBytesPerHour;
        bool hasSignificantGrowth = trend.GrowthPercentage > _leakGrowthPercentageThreshold;
        bool isMonotonic = IsMonotonicallyIncreasing(samples);

        trend.IsSuspectedLeak = isGrowing && (hasSignificantGrowth || isMonotonic);

        if (trend.IsSuspectedLeak)
        {
            if (growthRate > 1024 * 1024 * 50 || trend.GrowthPercentage > 100)
            {
                trend.LeakSeverity = "High";
            }
            else if (growthRate > 1024 * 1024 * 20 || trend.GrowthPercentage > 50)
            {
                trend.LeakSeverity = "Medium";
            }
            else
            {
                trend.LeakSeverity = "Low";
            }
        }
        else
        {
            trend.LeakSeverity = "None";
        }
    }

    private double CalculateGrowthRate(List<(DateTime Time, long Value)> samples)
    {
        if (samples.Count < 2)
        {
            return 0;
        }

        var times = samples.Select(s => (s.Time - _startTime).TotalHours).ToArray();
        var values = samples.Select(s => (double)s.Value).ToArray();

        double meanTime = times.Average();
        double meanValue = values.Average();

        double covariance = 0;
        double variance = 0;

        for (int i = 0; i < samples.Count; i++)
        {
            covariance += (times[i] - meanTime) * (values[i] - meanValue);
            variance += Math.Pow(times[i] - meanTime, 2);
        }

        if (Math.Abs(variance) < 0.0001)
        {
            return 0;
        }

        double slope = covariance / variance;
        return slope;
    }

    private bool IsMonotonicallyIncreasing(List<(DateTime Time, long Value)> samples)
    {
        if (samples.Count < 3)
        {
            return false;
        }

        int increaseCount = 0;
        for (int i = 1; i < samples.Count; i++)
        {
            if (samples[i].Value > samples[i - 1].Value)
            {
                increaseCount++;
            }
        }

        return (double)increaseCount / (samples.Count - 1) > 0.8;
    }

    public List<ProcessMemoryTrend> GetSuspectedLeaks()
    {
        return _processTrends.Values
            .Where(t => t.IsSuspectedLeak && t.Samples.Count >= _minSamplesForAnalysis)
            .OrderByDescending(t => t.GrowthRateBytesPerHour)
            .ToList();
    }

    public List<ProcessMemoryTrend> GetAllTrends()
    {
        return _processTrends.Values
            .OrderByDescending(t => t.GrowthRateBytesPerHour)
            .ToList();
    }

    public LeakReport GenerateReport()
    {
        var endMemory = _analyzer.GetPhysicalMemoryDistribution();
        var duration = DateTime.Now - _startTime;

        var report = new LeakReport
        {
            StartTime = _startTime,
            EndTime = DateTime.Now,
            Duration = duration,
            StartMemory = _startMemory ?? new PhysicalMemoryDistribution(),
            EndMemory = endMemory,
            SuspectedProcesses = GetSuspectedLeaks()
        };

        var systemGrowth = (long)(endMemory.ProcessPrivate + endMemory.NonPagedPool + endMemory.PagedPool) -
                          (long)(report.StartMemory.ProcessPrivate + report.StartMemory.NonPagedPool + report.StartMemory.PagedPool);

        if (systemGrowth > 0)
        {
            report.Notes = $"系统内存增长: {MemoryFormatter.FormatBytes(systemGrowth)} over {duration.TotalHours:F1}小时. ";
            report.Notes += $"共有 {report.SuspectedProcesses.Count} 个进程被怀疑存在内存泄漏.";
        }
        else
        {
            report.Notes = "监控期间系统内存使用稳定. 未检测到显著的内存泄漏.";
        }

        return report;
    }

    public async Task<LeakReport> RunLeakScan(int durationHours = 24, int sampleIntervalSeconds = 60, IProgress<string>? progress = null)
    {
        StartMonitoring();
        var endTime = _startTime.AddHours(durationHours);
        int sampleCount = 0;
        int totalSamples = (int)(durationHours * 3600 / sampleIntervalSeconds);

        while (DateTime.Now < endTime)
        {
            try
            {
                var sample = new MemorySample
                {
                    Timestamp = DateTime.Now,
                    SystemMemory = _analyzer.GetPhysicalMemoryDistribution(),
                    Processes = _analyzer.GetAllProcessMemoryInfo(),
                    PoolTags = _analyzer.GetPoolTagInformation()
                };

                AddSample(sample);
                sampleCount++;

                var progressMsg = $"[内存泄漏扫描] 进度: {sampleCount}/{totalSamples} 样本 | " +
                                 $"已运行: {(DateTime.Now - _startTime).TotalHours:F1}小时 | " +
                                 $"疑似泄漏: {GetSuspectedLeaks().Count} 个进程";

                progress?.Report(progressMsg);

                var remaining = endTime - DateTime.Now;
                if (remaining.TotalSeconds > sampleIntervalSeconds)
                {
                    await Task.Delay(sampleIntervalSeconds * 1000);
                }
                else if (remaining > TimeSpan.Zero)
                {
                    await Task.Delay(remaining);
                }
            }
            catch (Exception ex)
            {
                progress?.Report($"错误: {ex.Message}");
                await Task.Delay(5000);
            }
        }

        return GenerateReport();
    }
}
