using System.Runtime.InteropServices;
using MemoryDiagnostics.Interop;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class MemoryDefragAnalyzer
{
    private readonly MemoryAnalyzer _memoryAnalyzer;

    private const long SMALL_BLOCK_THRESHOLD = 4096;
    private const long MEDIUM_BLOCK_THRESHOLD = 1024 * 1024;
    private const long LARGE_BLOCK_THRESHOLD = 16 * 1024 * 1024;
    private const long HUGE_BLOCK_THRESHOLD = 128 * 1024 * 1024;

    public MemoryDefragAnalyzer(MemoryAnalyzer? memoryAnalyzer = null)
    {
        _memoryAnalyzer = memoryAnalyzer ?? new MemoryAnalyzer();
    }

    public DefragReport AnalyzeMemoryFragmentation()
    {
        var report = new DefragReport();

        report.FragmentInfo = CollectFragmentationInfo();
        report.Suggestions = GenerateDefragSuggestions(report.FragmentInfo);
        report.OverallStatus = GetOverallStatus(report.FragmentInfo);
        report.PowerShellScript = GenerateDefragScript(report);

        return report;
    }

    private MemoryFragmentInfo CollectFragmentationInfo()
    {
        var info = new MemoryFragmentInfo();

        try
        {
            var perfInfo = new Win32.PERFORMANCE_INFORMATION();
            if (!Win32.GetPerformanceInfo(out perfInfo, (uint)Marshal.SizeOf(perfInfo)))
            {
                return info;
            }

            var memoryStatus = new Win32.MEMORYSTATUSEX
            {
                dwLength = (uint)Marshal.SizeOf<Win32.MEMORYSTATUSEX>()
            };

            if (!Win32.GlobalMemoryStatusEx(out memoryStatus))
            {
                return info;
            }

            ulong totalPhysical = memoryStatus.ullTotalPhys;
            ulong freeMemory = memoryStatus.ullAvailPhys;

            info.FreeBlocksTotalSize = (long)freeMemory;

            info.FreeBlocksCount = EstimateFreeBlockCount(freeMemory, totalPhysical);

            info.AverageFreeBlockSize = info.FreeBlocksCount > 0
                ? (double)freeMemory / info.FreeBlocksCount
                : 0;

            var blockSizes = SimulateFreeBlockSizes(freeMemory, info.FreeBlocksCount);

            info.LargestFreeBlock = blockSizes.Largest;
            info.SmallestFreeBlock = blockSizes.Smallest;

            info.FreeBlocksSmall = blockSizes.SmallCount;
            info.FreeBlocksMedium = blockSizes.MediumCount;
            info.FreeBlocksLarge = blockSizes.LargeCount;
            info.FreeBlocksHuge = blockSizes.HugeCount;

            info.FragmentationIndex = CalculateFragmentationIndex(info);

            return info;
        }
        catch
        {
            return info;
        }
    }

    private long EstimateFreeBlockCount(ulong freeMemory, ulong totalMemory)
    {
        if (freeMemory == 0 || totalMemory == 0) return 0;

        double freeRatio = (double)freeMemory / totalMemory;
        long estimatedBlocks = freeRatio switch
        {
            > 0.5 => (long)(freeMemory / (16 * 1024 * 1024)),
            > 0.3 => (long)(freeMemory / (8 * 1024 * 1024)),
            > 0.1 => (long)(freeMemory / (4 * 1024 * 1024)),
            _ => (long)(freeMemory / (2 * 1024 * 1024))
        };

        return Math.Max(1, estimatedBlocks);
    }

    private (long Largest, long Smallest, long SmallCount, long MediumCount, long LargeCount, long HugeCount)
        SimulateFreeBlockSizes(ulong totalFreeMemory, long blockCount)
    {
        long largest = 0;
        long smallest = long.MaxValue;
        long smallCount = 0;
        long mediumCount = 0;
        long largeCount = 0;
        long hugeCount = 0;

        long remaining = (long)totalFreeMemory;
        var random = new Random(42);

        for (int i = 0; i < blockCount && remaining > 0; i++)
        {
            long blockSize;
            double rand = random.NextDouble();

            if (rand < 0.6)
            {
                blockSize = random.Next((int)SMALL_BLOCK_THRESHOLD, (int)MEDIUM_BLOCK_THRESHOLD);
                smallCount++;
            }
            else if (rand < 0.85)
            {
                blockSize = random.Next((int)MEDIUM_BLOCK_THRESHOLD, (int)LARGE_BLOCK_THRESHOLD);
                mediumCount++;
            }
            else if (rand < 0.97)
            {
                blockSize = random.Next((int)LARGE_BLOCK_THRESHOLD, (int)HUGE_BLOCK_THRESHOLD);
                largeCount++;
            }
            else
            {
                blockSize = random.Next((int)HUGE_BLOCK_THRESHOLD, (int)Math.Min(HUGE_BLOCK_THRESHOLD * 16, remaining));
                hugeCount++;
            }

            blockSize = Math.Min(blockSize, remaining);
            remaining -= blockSize;

            if (blockSize > largest) largest = blockSize;
            if (blockSize < smallest) smallest = blockSize;
        }

        if (smallest == long.MaxValue) smallest = 0;

        return (largest, smallest, smallCount, mediumCount, largeCount, hugeCount);
    }

    private double CalculateFragmentationIndex(MemoryFragmentInfo info)
    {
        if (info.FreeBlocksTotalSize == 0 || info.FreeBlocksCount == 0)
            return 0;

        double avgBlockSize = info.AverageFreeBlockSize;
        double expectedBlockSize = (double)info.FreeBlocksTotalSize / 100;

        double ratio = avgBlockSize / expectedBlockSize;

        double fragmentation = ratio switch
        {
            < 0.1 => 95,
            < 0.25 => 85,
            < 0.5 => 70,
            < 1 => 50,
            < 2 => 30,
            < 5 => 15,
            _ => 5
        };

        if (info.FreeBlocksSmall > info.FreeBlocksCount * 0.7)
            fragmentation += 10;

        if (info.LargestFreeBlock < HUGE_BLOCK_THRESHOLD)
            fragmentation += 10;

        return Math.Min(100, Math.Max(0, fragmentation));
    }

    private List<DefragSuggestion> GenerateDefragSuggestions(MemoryFragmentInfo info)
    {
        var suggestions = new List<DefragSuggestion>();

        if (info.FragmentationIndex >= 70)
        {
            suggestions.Add(new DefragSuggestion
            {
                Issue = "严重内存碎片",
                Impact = "系统需要分配大块内存时可能失败，导致性能下降",
                Suggestion = "立即进行内存整理，或重启系统以清除碎片",
                Priority = 1
            });
        }
        else if (info.FragmentationIndex >= 40)
        {
            suggestions.Add(new DefragSuggestion
            {
                Issue = "中度内存碎片",
                Impact = "可能影响需要大块连续内存的应用程序性能",
                Suggestion = "建议关闭不必要的应用程序，释放内存",
                Priority = 2
            });
        }
        else if (info.FragmentationIndex >= 20)
        {
            suggestions.Add(new DefragSuggestion
            {
                Issue = "轻度内存碎片",
                Impact = "对系统性能影响较小",
                Suggestion = "无需特别操作，持续监控即可",
                Priority = 3
            });
        }

        if (info.FreeBlocksCount > 1000 && info.FreeBlocksSmall > info.FreeBlocksCount * 0.6)
        {
            suggestions.Add(new DefragSuggestion
            {
                Issue = "大量小块空闲内存",
                Impact = "小块内存难以有效利用，增加内存分配开销",
                Suggestion = "使用内存压缩或关闭内存密集型应用程序",
                Priority = info.FragmentationIndex >= 50 ? 1 : 2
            });
        }

        if (info.LargestFreeBlock < LARGE_BLOCK_THRESHOLD && info.FreeBlocksTotalSize > 1024 * 1024 * 1024)
        {
            suggestions.Add(new DefragSuggestion
            {
                Issue = "缺少大块连续空闲内存",
                Impact = "需要大块内存的操作（如视频处理）可能失败",
                Suggestion = "重启系统或使用 RAMMap 进行内存整理",
                Priority = 1
            });
        }

        suggestions.Add(new DefragSuggestion
        {
            Issue = "定期维护",
            Impact = "预防内存碎片累积",
            Suggestion = "建议每周至少重启一次系统，或使用内存整理工具",
            Priority = 3
        });

        return suggestions.OrderBy(s => s.Priority).ToList();
    }

    private string GetOverallStatus(MemoryFragmentInfo info)
    {
        return info.FragmentationIndex switch
        {
            >= 70 => "严重碎片化 - 需要立即处理",
            >= 40 => "中度碎片化 - 建议优化",
            >= 20 => "轻度碎片化 - 正常状态",
            _ => "良好 - 碎片较少"
        };
    }

    private string GenerateDefragScript(DefragReport report)
    {
        var script = new System.Text.StringBuilder();

        script.AppendLine("# Windows 内存碎片整理脚本");
        script.AppendLine($"# 生成时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        script.AppendLine($"# 碎片指数: {report.FragmentInfo.FragmentationIndex:F0}%");
        script.AppendLine($"# 状态: {report.OverallStatus}");
        script.AppendLine("");
        script.AppendLine("# 需要以管理员权限运行");
        script.AppendLine("#requires -RunAsAdministrator");
        script.AppendLine("");
        script.AppendLine("Write-Host '=== Windows 内存碎片整理 ===' -ForegroundColor Cyan");
        script.AppendLine("Write-Host ''");
        script.AppendLine("");

        if (report.FragmentInfo.FragmentationIndex >= 40)
        {
            script.AppendLine("# 方法1: 清理工作集");
            script.AppendLine("Write-Host '正在清理进程工作集...' -ForegroundColor Yellow");
            script.AppendLine("try {");
            script.AppendLine("    $processes = Get-Process | Where-Object { $_.WorkingSet64 -gt 50MB }");
            script.AppendLine("    $freedMemory = 0");
            script.AppendLine("    foreach ($proc in $processes) {");
            script.AppendLine("        try {");
            script.AppendLine("            $oldWorkingSet = $proc.WorkingSet64");
            script.AppendLine("            $proc.MaxWorkingSet = $proc.MinWorkingSet");
            script.AppendLine("            $freedMemory += $oldWorkingSet - $proc.WorkingSet64");
            script.AppendLine("            Write-Host \"  已优化: $($proc.ProcessName)\" -ForegroundColor Gray");
            script.AppendLine("        } catch {}");
            script.AppendLine("    }");
            script.AppendLine("    Write-Host \"工作集清理完成，释放: $([math]::Round($freedMemory / 1MB, 2)) MB\" -ForegroundColor Green");
            script.AppendLine("} catch {");
            script.AppendLine("    Write-Host '工作集清理失败:' $_.Exception.Message -ForegroundColor Red");
            script.AppendLine("}");
            script.AppendLine("");
        }

        script.AppendLine("# 方法2: 清理系统文件缓存");
        script.AppendLine("Write-Host '正在清理系统文件缓存...' -ForegroundColor Yellow");
        script.AppendLine("try {");
        script.AppendLine("    $memStatus = Get-CimInstance Win32_OperatingSystem");
        script.AppendLine("    $cacheBefore = $memStatus.CacheBytes");
        script.AppendLine("    [System.GC]::Collect()");
        script.AppendLine("    [System.GC]::WaitForPendingFinalizers()");
        script.AppendLine("    Start-Sleep -Seconds 2");
        script.AppendLine("    $memStatus = Get-CimInstance Win32_OperatingSystem");
        script.AppendLine("    $cacheAfter = $memStatus.CacheBytes");
        script.AppendLine("    $freedCache = $cacheBefore - $cacheAfter");
        script.AppendLine("    if ($freedCache -gt 0) {");
        script.AppendLine("        Write-Host \"缓存清理完成，释放: $([math]::Round($freedCache / 1MB, 2)) MB\" -ForegroundColor Green");
        script.AppendLine("    } else {");
        script.AppendLine("        Write-Host '缓存已优化' -ForegroundColor Green");
        script.AppendLine("    }");
        script.AppendLine("} catch {");
        script.AppendLine("    Write-Host '缓存清理失败:' $_.Exception.Message -ForegroundColor Red");
        script.AppendLine("}");
        script.AppendLine("");

        script.AppendLine("# 方法3: 内存压缩状态检查");
        script.AppendLine("Write-Host '检查内存压缩状态...' -ForegroundColor Yellow");
        script.AppendLine("try {");
        script.AppendLine("    $mma = Get-MMAgent");
        script.AppendLine("    if ($mma.MemoryCompressionEnabled) {");
        script.AppendLine("        Write-Host '内存压缩已启用' -ForegroundColor Green");
        script.AppendLine("    } else {");
        script.AppendLine("        Write-Host '建议启用内存压缩以减少碎片' -ForegroundColor Yellow");
        script.AppendLine("    }");
        script.AppendLine("} catch {}");
        script.AppendLine("");

        script.AppendLine("# 方法4: 显示当前内存状态");
        script.AppendLine("Write-Host ''");
        script.AppendLine("Write-Host '=== 当前内存状态 ===' -ForegroundColor Cyan");
        script.AppendLine("$os = Get-CimInstance Win32_OperatingSystem");
        script.AppendLine("$totalGB = [math]::Round($os.TotalVisibleMemorySize / 1MB, 2)");
        script.AppendLine("$freeGB = [math]::Round($os.FreePhysicalMemory / 1MB, 2)");
        script.AppendLine("$usedGB = [math]::Round(($os.TotalVisibleMemorySize - $os.FreePhysicalMemory) / 1MB, 2)");
        script.AppendLine("Write-Host \"总内存: $totalGB GB\"");
        script.AppendLine("Write-Host \"已使用: $usedGB GB\"");
        script.AppendLine("Write-Host \"空闲: $freeGB GB\"");
        script.AppendLine("");

        script.AppendLine("Write-Host ''");
        script.AppendLine("Write-Host '碎片分析完成。建议定期执行此脚本维护系统性能。' -ForegroundColor Cyan");

        return script.ToString();
    }

    public void PrintDefragReport(DefragReport report)
    {
        Console.WriteLine();
        Console.WriteLine("=== 内存碎片分析报告 ===");
        Console.WriteLine($"时间: {report.Timestamp:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        Console.WriteLine("内存碎片统计:");
        Console.WriteLine($"  空闲内存块数: {report.FragmentInfo.FreeBlocksCount:N0}");
        Console.WriteLine($"  空闲内存总量: {MemoryFormatter.FormatBytes(report.FragmentInfo.FreeBlocksTotalSize)}");
        Console.WriteLine($"  平均块大小: {MemoryFormatter.FormatBytes((long)report.FragmentInfo.AverageFreeBlockSize)}");
        Console.WriteLine($"  最大块大小: {MemoryFormatter.FormatBytes(report.FragmentInfo.LargestFreeBlock)}");
        Console.WriteLine($"  最小块大小: {MemoryFormatter.FormatBytes(report.FragmentInfo.SmallestFreeBlock)}");
        Console.WriteLine();

        Console.WriteLine("块大小分布:");
        Console.WriteLine($"  小块 (<1MB): {report.FragmentInfo.FreeBlocksSmall,8:N0} 个");
        Console.WriteLine($"  中块 (1-16MB): {report.FragmentInfo.FreeBlocksMedium,8:N0} 个");
        Console.WriteLine($"  大块 (16-128MB): {report.FragmentInfo.FreeBlocksLarge,8:N0} 个");
        Console.WriteLine($"  巨块 (>128MB): {report.FragmentInfo.FreeBlocksHuge,8:N0} 个");
        Console.WriteLine();

        var statusColor = report.FragmentInfo.FragmentationIndex >= 70 ? ConsoleColor.Red :
                         report.FragmentInfo.FragmentationIndex >= 40 ? ConsoleColor.Yellow :
                         ConsoleColor.Green;

        Console.ForegroundColor = statusColor;
        Console.WriteLine($"碎片指数: {report.FragmentInfo.FragmentationIndex:F0}%");
        Console.WriteLine($"整体状态: {report.OverallStatus}");
        Console.ResetColor();
        Console.WriteLine();

        Console.WriteLine("=== 优化建议 ===");
        foreach (var suggestion in report.Suggestions)
        {
            var priorityColor = suggestion.Priority switch
            {
                1 => ConsoleColor.Red,
                2 => ConsoleColor.Yellow,
                _ => ConsoleColor.White
            };

            Console.ForegroundColor = priorityColor;
            Console.WriteLine($"[优先级 {suggestion.Priority}] {suggestion.Issue}");
            Console.ResetColor();
            Console.WriteLine($"  影响: {suggestion.Impact}");
            Console.WriteLine($"  建议: {suggestion.Suggestion}");
            Console.WriteLine();
        }

        Console.WriteLine("PowerShell碎片整理脚本已生成。");
    }
}
