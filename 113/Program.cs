using MemoryDiagnostics.Core;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics;

class Program
{
    static async Task<int> Main(string[] args)
    {
        var options = ParseArguments(args);

        if (options.ShowHelp || args.Length == 0)
        {
            ShowHelp();
            return 0;
        }

        try
        {
            var analyzer = new MemoryAnalyzer { FastMode = options.FastMode };
            var exporter = new ReportExporter();

            if (options.ShowDedupSuggest)
            {
                await ShowDedupSuggestions(analyzer, options.OutputFile);
            }

            if (options.ShowDefragSuggest)
            {
                await ShowDefragSuggestions(analyzer, options.OutputFile);
            }

            if (options.ShowSummary)
            {
                await ShowMemorySummary(analyzer);
            }

            if (options.ShowTop)
            {
                await ShowTopProcesses(analyzer, options.TopCount);
            }

            if (options.ShowPool)
            {
                await ShowPoolTags(analyzer, options.PoolTagCount);
            }

            if (options.RunLeakScan)
            {
                await RunLeakScan(analyzer, exporter, options.LeakScanDurationHours, options.LeakScanIntervalSeconds, options.OutputFile, options.FastMode);
            }

            if (options.RunRealTimeMonitor)
            {
                await RunRealTimeMonitor(options.MonitorDurationSeconds, options.SamplingIntervalMs, options.OutputFile, options.FastMode);
            }

            if (!string.IsNullOrEmpty(options.OutputFile) && !options.RunLeakScan && !options.RunRealTimeMonitor && !options.ShowDedupSuggest && !options.ShowDefragSuggest)
            {
                await ExportCurrentSnapshot(analyzer, exporter, options.OutputFile);
            }

            return 0;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"错误: {ex.Message}");
            Console.Error.WriteLine(ex.StackTrace);
            return 1;
        }
    }

    static CommandLineOptions ParseArguments(string[] args)
    {
        var options = new CommandLineOptions();

        for (int i = 0; i < args.Length; i++)
        {
            switch (args[i].ToLower())
            {
                case "--help":
                case "-h":
                case "/?":
                    options.ShowHelp = true;
                    break;
                case "--summary":
                case "-s":
                    options.ShowSummary = true;
                    break;
                case "--top":
                    options.ShowTop = true;
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int topCount))
                    {
                        options.TopCount = topCount;
                        i++;
                    }
                    break;
                case "--pool":
                    options.ShowPool = true;
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int poolCount))
                    {
                        options.PoolTagCount = poolCount;
                        i++;
                    }
                    break;
                case "--leak-scan":
                    options.RunLeakScan = true;
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int duration))
                    {
                        options.LeakScanDurationHours = duration;
                        i++;
                    }
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int interval))
                    {
                        options.LeakScanIntervalSeconds = interval;
                        i++;
                    }
                    break;
                case "--monitor":
                case "-m":
                    options.RunRealTimeMonitor = true;
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int monitorDuration))
                    {
                        options.MonitorDurationSeconds = monitorDuration;
                        i++;
                    }
                    break;
                case "--interval":
                    if (i + 1 < args.Length && int.TryParse(args[i + 1], out int samplingInterval))
                    {
                        options.SamplingIntervalMs = samplingInterval;
                        i++;
                    }
                    break;
                case "--fast":
                case "-f":
                    options.FastMode = true;
                    break;
                case "--dedup-suggest":
                    options.ShowDedupSuggest = true;
                    break;
                case "--defrag-suggest":
                    options.ShowDefragSuggest = true;
                    break;
                case "--output":
                case "-o":
                    if (i + 1 < args.Length)
                    {
                        options.OutputFile = args[i + 1];
                        i++;
                    }
                    break;
                default:
                    Console.Error.WriteLine($"警告: 未知参数 {args[i]}");
                    break;
            }
        }

        if (!options.ShowTop && !options.ShowPool && !options.RunLeakScan && !options.RunRealTimeMonitor &&
            !options.ShowDedupSuggest && !options.ShowDefragSuggest)
        {
            options.ShowSummary = true;
        }

        return options;
    }

    static void ShowHelp()
    {
        Console.WriteLine("=== Windows 内存诊断 CLI 工具 ===");
        Console.WriteLine();
        Console.WriteLine("用法: MemoryDiagnostics [选项]");
        Console.WriteLine();
        Console.WriteLine("选项:");
        Console.WriteLine("  --help, -h, /?          显示此帮助信息");
        Console.WriteLine("  --summary, -s           显示物理内存使用分布摘要");
        Console.WriteLine("  --top [N]               显示占用内存前N的进程 (默认10)");
        Console.WriteLine("  --pool [N]              显示分页池/非分页池按tag分组的内存分配 (默认20)");
        Console.WriteLine("  --leak-scan [H] [I]     运行内存泄漏扫描, H=小时(默认24), I=间隔秒(默认60)");
        Console.WriteLine("  --monitor [S]           实时监控内存变化, S=秒(默认60)");
        Console.WriteLine("  --interval [MS]         ETW采样间隔毫秒 (默认1000)");
        Console.WriteLine("  --fast, -f              快速模式, 低CPU占用, 只取概要信息");
        Console.WriteLine("  --dedup-suggest         分析重复DLL和文件缓存, 提供去重建议");
        Console.WriteLine("  --defrag-suggest        分析内存碎片, 生成碎片整理建议");
        Console.WriteLine("  --output, -o <文件>     导出报告到文件 (.json, .csv, .html, .ps1)");
        Console.WriteLine();
        Console.WriteLine("示例:");
        Console.WriteLine("  MemoryDiagnostics --summary                # 显示内存摘要");
        Console.WriteLine("  MemoryDiagnostics --top 15                 # 显示前15个进程");
        Console.WriteLine("  MemoryDiagnostics --pool 30                # 显示前30个Pool Tag");
        Console.WriteLine("  MemoryDiagnostics --leak-scan 24 60        # 24小时泄漏扫描, 60秒间隔");
        Console.WriteLine("  MemoryDiagnostics --monitor 120 --interval 500  # 2分钟实时监控, 500ms采样");
        Console.WriteLine("  MemoryDiagnostics --dedup-suggest          # 内存去重建议");
        Console.WriteLine("  MemoryDiagnostics --defrag-suggest         # 内存碎片建议");
        Console.WriteLine("  MemoryDiagnostics --fast --summary         # 快速模式内存摘要");
        Console.WriteLine("  MemoryDiagnostics --fast --monitor 300     # 快速模式5分钟监控");
        Console.WriteLine("  MemoryDiagnostics --summary --output report.json  # 导出为JSON");
        Console.WriteLine("  MemoryDiagnostics --dedup-suggest --output report.html  # 导出HTML报告");
        Console.WriteLine("  MemoryDiagnostics --defrag-suggest --output script.ps1  # 导出PowerShell脚本");
        Console.WriteLine();
        Console.WriteLine("注意: ETW功能和Pool Tag分析需要管理员权限运行。");
        Console.WriteLine("      --fast 模式可显著降低CPU占用(<1%),适用于长时间监控。");
    }

    static async Task ShowMemorySummary(MemoryAnalyzer analyzer)
    {
        Console.WriteLine("=== 物理内存使用分布 ===");
        Console.WriteLine($"时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        var mem = analyzer.GetPhysicalMemoryDistribution();
        var total = mem.TotalPhysical;

        Console.WriteLine($"{"内存类型",-20} {"大小",15} {"占比",8}");
        Console.WriteLine(new string('-', 50));

        PrintMemoryLine("总物理内存", (long)total, total);
        PrintMemoryLine("  进程私有内存", (long)mem.ProcessPrivate, total);
        PrintMemoryLine("  共享内存", (long)mem.SharedMemory, total);
        PrintMemoryLine("  分页池", (long)mem.PagedPool, total);
        PrintMemoryLine("  非分页池", (long)mem.NonPagedPool, total);
        PrintMemoryLine("  系统缓存", (long)mem.SystemCache, total);
        PrintMemoryLine("  驱动锁定内存", (long)mem.DriverLocked, total);
        PrintMemoryLine("  空闲内存", (long)mem.FreeMemory, total);

        var accounted = mem.ProcessPrivate + mem.SharedMemory + mem.PagedPool +
                       mem.NonPagedPool + mem.SystemCache + mem.DriverLocked + mem.FreeMemory;

        if (accounted != total)
        {
            Console.WriteLine();
            Console.WriteLine($"统计差异: {MemoryFormatter.FormatBytes((long)Math.Abs((long)(accounted - total)))}");
        }

        await Task.CompletedTask;
    }

    static void PrintMemoryLine(string label, long bytes, ulong total)
    {
        var percentage = total > 0 ? (double)bytes / total * 100 : 0;
        Console.WriteLine($"{label,-20} {MemoryFormatter.FormatBytes(bytes),15} {percentage,7:0.00}%");
    }

    static async Task ShowTopProcesses(MemoryAnalyzer analyzer, int topN)
    {
        Console.WriteLine();
        Console.WriteLine($"=== 内存占用前 {topN} 进程 ===");
        Console.WriteLine($"时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        var processes = analyzer.GetTopProcessesByMemory(topN);

        Console.WriteLine($"{"PID",8} {"进程名",25} {"工作集",15} {"私有工作集",15} {"共享工作集",15} {"私有字节",15}");
        Console.WriteLine(new string('-', 105));

        foreach (var proc in processes)
        {
            Console.WriteLine($"{proc.ProcessId,8} {Truncate(proc.ProcessName, 25),25} " +
                            $"{MemoryFormatter.FormatBytes(proc.WorkingSetSize),15} " +
                            $"{MemoryFormatter.FormatBytes(proc.PrivateWorkingSet),15} " +
                            $"{MemoryFormatter.FormatBytes(proc.SharedWorkingSet),15} " +
                            $"{MemoryFormatter.FormatBytes(proc.PrivateBytes),15}");
        }

        await Task.CompletedTask;
    }

    static async Task ShowPoolTags(MemoryAnalyzer analyzer, int topN)
    {
        Console.WriteLine();
        Console.WriteLine($"=== 分页池/非分页池内存分配 (前 {topN} 个Tag) ===");
        Console.WriteLine($"时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        var poolTags = analyzer.GetPoolTagInformation().Take(topN).ToList();

        Console.WriteLine($"{"Tag",12} {"Tag名称",8} {"分页池大小",15} {"非分页池大小",15} {"总大小",15}");
        Console.WriteLine(new string('-', 75));

        foreach (var tag in poolTags)
        {
            Console.WriteLine($"{tag.Tag,12} {tag.TagName,8} " +
                            $"{MemoryFormatter.FormatBytes(tag.PagedUsed),15} " +
                            $"{MemoryFormatter.FormatBytes(tag.NonPagedUsed),15} " +
                            $"{MemoryFormatter.FormatBytes(tag.PagedUsed + tag.NonPagedUsed),15}");
        }

        await Task.CompletedTask;
    }

    static async Task ShowDedupSuggestions(MemoryAnalyzer analyzer, string? outputFile)
    {
        Console.WriteLine("正在分析重复DLL和文件缓存...");

        var dedupAnalyzer = new MemoryDedupAnalyzer(analyzer);
        var dedupReport = dedupAnalyzer.AnalyzeDedupOpportunities();

        dedupAnalyzer.PrintDedupReport(dedupReport);

        if (!string.IsNullOrEmpty(outputFile))
        {
            var ext = Path.GetExtension(outputFile).ToLower();

            if (ext == ".html")
            {
                var beforeMemory = analyzer.GetPhysicalMemoryDistribution();
                var htmlGenerator = new HtmlReportGenerator();
                var html = htmlGenerator.GenerateOptimizationReport(beforeMemory, dedupReport, null, dedupReport.TotalPotentialSavings);
                await File.WriteAllTextAsync(outputFile, html);
                Console.WriteLine($"\nHTML报告已导出到: {outputFile}");
            }
            else if (ext == ".ps1")
            {
                await File.WriteAllTextAsync(outputFile, dedupReport.PowerShellScript);
                Console.WriteLine($"\nPowerShell脚本已导出到: {outputFile}");
            }
            else
            {
                var exporter = new ReportExporter();
                var format = ReportExporter.DetectFormatFromFileName(outputFile);
                await exporter.ExportToJsonAsync(new { DedupReport = dedupReport }, outputFile);
                Console.WriteLine($"\n报告已导出到: {outputFile}");
            }
        }
    }

    static async Task ShowDefragSuggestions(MemoryAnalyzer analyzer, string? outputFile)
    {
        Console.WriteLine("正在分析内存碎片...");

        var defragAnalyzer = new MemoryDefragAnalyzer(analyzer);
        var defragReport = defragAnalyzer.AnalyzeMemoryFragmentation();

        defragAnalyzer.PrintDefragReport(defragReport);

        if (!string.IsNullOrEmpty(outputFile))
        {
            var ext = Path.GetExtension(outputFile).ToLower();

            if (ext == ".html")
            {
                var beforeMemory = analyzer.GetPhysicalMemoryDistribution();
                var htmlGenerator = new HtmlReportGenerator();
                var html = htmlGenerator.GenerateOptimizationReport(beforeMemory, null, defragReport, 0);
                await File.WriteAllTextAsync(outputFile, html);
                Console.WriteLine($"\nHTML报告已导出到: {outputFile}");
            }
            else if (ext == ".ps1")
            {
                await File.WriteAllTextAsync(outputFile, defragReport.PowerShellScript);
                Console.WriteLine($"\nPowerShell脚本已导出到: {outputFile}");
            }
            else
            {
                var exporter = new ReportExporter();
                var format = ReportExporter.DetectFormatFromFileName(outputFile);
                await exporter.ExportToJsonAsync(new { DefragReport = defragReport }, outputFile);
                Console.WriteLine($"\n报告已导出到: {outputFile}");
            }
        }
    }

    static async Task RunLeakScan(MemoryAnalyzer analyzer, ReportExporter exporter, int durationHours, int intervalSeconds, string? outputFile, bool fastMode)
    {
        Console.WriteLine();
        Console.WriteLine("=== 内存泄漏扫描模式 ===");
        Console.WriteLine($"扫描时长: {durationHours} 小时");
        Console.WriteLine($"采样间隔: {intervalSeconds} 秒");
        Console.WriteLine($"快速模式: {(fastMode ? "开启 (低CPU)" : "关闭 (完整分析)")}");
        Console.WriteLine($"开始时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine("按 Ctrl+C 可以提前终止扫描并生成报告。");
        Console.WriteLine();

        var leakDetector = new LeakDetector(fastMode);
        var cts = new CancellationTokenSource();

        Console.CancelKeyPress += (s, e) =>
        {
            Console.WriteLine("\n正在终止扫描并生成报告...");
            cts.Cancel();
            e.Cancel = true;
        };

        var progress = new Progress<string>(msg =>
        {
            Console.Write($"\r{msg}    ");
        });

        try
        {
            var scanTask = leakDetector.RunLeakScan(durationHours, intervalSeconds, progress);
            var timeoutTask = Task.Delay(Timeout.Infinite, cts.Token);

            var completedTask = await Task.WhenAny(scanTask, timeoutTask);

            if (completedTask == timeoutTask)
            {
                Console.WriteLine("\n用户中断，生成当前报告...");
            }

            var report = scanTask.IsCompleted ? scanTask.Result : leakDetector.GenerateReport();

            Console.WriteLine();
            Console.WriteLine();
            Console.WriteLine("=== 内存泄漏扫描报告 ===");
            Console.WriteLine($"开始时间: {report.StartTime:yyyy-MM-dd HH:mm:ss}");
            Console.WriteLine($"结束时间: {report.EndTime:yyyy-MM-dd HH:mm:ss}");
            Console.WriteLine($"持续时间: {report.Duration.TotalHours:F2} 小时");
            Console.WriteLine();
            Console.WriteLine(report.Notes);
            Console.WriteLine();

            if (report.SuspectedProcesses.Count > 0)
            {
                Console.WriteLine($"发现 {report.SuspectedProcesses.Count} 个疑似内存泄漏的进程:");
                Console.WriteLine();
                Console.WriteLine($"{"PID",8} {"进程名",25} {"增长率",18} {"严重程度",10} {"初始值",15} {"当前值",15} {"增长%",10}");
                Console.WriteLine(new string('-', 115));

                foreach (var proc in report.SuspectedProcesses)
                {
                    var severityColor = proc.LeakSeverity switch
                    {
                        "High" => ConsoleColor.Red,
                        "Medium" => ConsoleColor.Yellow,
                        "Low" => ConsoleColor.Cyan,
                        _ => ConsoleColor.White
                    };

                    Console.ForegroundColor = severityColor;
                    Console.Write($"{proc.ProcessId,8} {Truncate(proc.ProcessName, 25),25} ");
                    Console.ResetColor();
                    Console.WriteLine($"{MemoryFormatter.FormatBytes((long)proc.GrowthRateBytesPerHour) + "/h",18} " +
                                    $"{proc.LeakSeverity,10} " +
                                    $"{MemoryFormatter.FormatBytes(proc.InitialValue),15} " +
                                    $"{MemoryFormatter.FormatBytes(proc.CurrentValue),15} " +
                                    $"{proc.GrowthPercentage,9:0.00}%");
                }
            }
            else
            {
                Console.WriteLine("未发现明显的内存泄漏。");
            }

            if (!string.IsNullOrEmpty(outputFile))
            {
                var format = ReportExporter.DetectFormatFromFileName(outputFile);
                await exporter.ExportLeakReportAsync(report, outputFile, format);
                Console.WriteLine();
                Console.WriteLine($"报告已导出到: {outputFile}");
            }
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("\n扫描已取消。");
        }
    }

    static async Task RunRealTimeMonitor(int durationSeconds, int samplingIntervalMs, string? outputFile, bool fastMode)
    {
        Console.WriteLine();
        Console.WriteLine("=== 实时内存监控模式 ===");
        Console.WriteLine($"监控时长: {durationSeconds} 秒 (按 Ctrl+C 停止)");
        Console.WriteLine($"采样间隔: {samplingIntervalMs} 毫秒");
        Console.WriteLine($"快速模式: {(fastMode ? "开启 (低CPU)" : "关闭 (完整分析)")}");
        Console.WriteLine($"开始时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        using var monitor = new EtwMemoryMonitor(samplingIntervalMs, fastMode);
        var samples = new List<MemorySample>();
        var cts = new CancellationTokenSource();

        Console.CancelKeyPress += (s, e) =>
        {
            Console.WriteLine("\n正在停止监控...");
            cts.Cancel();
            e.Cancel = true;
        };

        int sampleCount = 0;
        monitor.OnSampleCollected += sample =>
        {
            sampleCount++;
            samples.Add(sample);

            Console.Write($"\r样本 #{sampleCount} | " +
                         $"时间: {sample.Timestamp:HH:mm:ss} | " +
                         $"空闲: {MemoryFormatter.FormatBytes((long)sample.SystemMemory.FreeMemory)} | " +
                         $"进程数: {sample.Processes.Count} | " +
                         $"Pool Tags: {sample.PoolTags.Count}    ");

            if (sampleCount % 30 == 0)
            {
                Console.WriteLine();
                var topProc = sample.Processes.FirstOrDefault();
                if (topProc != null)
                {
                    Console.WriteLine($"  内存最高进程: {topProc.ProcessName} (PID: {topProc.ProcessId}) - {MemoryFormatter.FormatBytes(topProc.WorkingSetSize)}");
                }
            }
        };

        monitor.OnMemoryEvent += ev =>
        {
            if (ev.AllocationSize > 1024 * 1024 * 10)
            {
                Console.WriteLine();
                Console.WriteLine($"  [大额{ev.EventType}] PID:{ev.ProcessId} 大小:{MemoryFormatter.FormatBytes(ev.AllocationSize)} 地址:0x{ev.Address:X}");
            }
        };

        try
        {
            await monitor.StartMonitoring(cts.Token);
            await Task.Delay(durationSeconds * 1000, cts.Token);
        }
        catch (OperationCanceledException)
        {
        }
        finally
        {
            await monitor.StopMonitoring();
        }

        Console.WriteLine();
        Console.WriteLine();
        Console.WriteLine($"=== 监控结束 ===");
        Console.WriteLine($"采集样本数: {samples.Count}");
        Console.WriteLine($"监控时长: {(DateTime.Now - samples[0].Timestamp).TotalSeconds:F1} 秒");

        if (samples.Count > 1)
        {
            var first = samples[0].SystemMemory;
            var last = samples[^1].SystemMemory;
            Console.WriteLine();
            Console.WriteLine("内存变化:");
            Console.WriteLine($"  空闲内存: {MemoryFormatter.FormatBytes((long)first.FreeMemory)} -> {MemoryFormatter.FormatBytes((long)last.FreeMemory)} " +
                            $"({(last.FreeMemory >= first.FreeMemory ? "+" : "")}{MemoryFormatter.FormatBytes((long)(last.FreeMemory - first.FreeMemory))})");
            Console.WriteLine($"  私有内存: {MemoryFormatter.FormatBytes((long)first.ProcessPrivate)} -> {MemoryFormatter.FormatBytes((long)last.ProcessPrivate)} " +
                            $"({(last.ProcessPrivate >= first.ProcessPrivate ? "+" : "")}{MemoryFormatter.FormatBytes((long)(last.ProcessPrivate - first.ProcessPrivate))})");
        }

        if (!string.IsNullOrEmpty(outputFile) && samples.Count > 0)
        {
            var exporter = new ReportExporter();
            var format = ReportExporter.DetectFormatFromFileName(outputFile);

            if (format == ExportFormat.Json)
            {
                await exporter.ExportToJsonAsync(samples, outputFile);
            }
            else
            {
                for (int i = 0; i < samples.Count; i++)
                {
                    var sampleFile = Path.Combine(
                        Path.GetDirectoryName(outputFile) ?? ".",
                        $"{Path.GetFileNameWithoutExtension(outputFile)}_{i + 1}{Path.GetExtension(outputFile)}");
                    await exporter.ExportMemorySampleAsync(samples[i], sampleFile, format);
                }
            }
            Console.WriteLine();
            Console.WriteLine($"监控数据已导出到: {outputFile}");
        }
    }

    static async Task ExportCurrentSnapshot(MemoryAnalyzer analyzer, ReportExporter exporter, string outputFile)
    {
        var format = ReportExporter.DetectFormatFromFileName(outputFile);
        var sample = new MemorySample
        {
            Timestamp = DateTime.Now,
            SystemMemory = analyzer.GetPhysicalMemoryDistribution(),
            Processes = analyzer.GetAllProcessMemoryInfo(),
            PoolTags = analyzer.GetPoolTagInformation()
        };

        await exporter.ExportMemorySampleAsync(sample, outputFile, format);
        Console.WriteLine();
        Console.WriteLine($"当前内存快照已导出到: {outputFile}");
    }

    static string Truncate(string s, int maxLength)
    {
        if (string.IsNullOrEmpty(s)) return s;
        return s.Length <= maxLength ? s : s.Substring(0, maxLength - 3) + "...";
    }
}

class CommandLineOptions
{
    public bool ShowHelp { get; set; }
    public bool ShowSummary { get; set; }
    public bool ShowTop { get; set; }
    public int TopCount { get; set; } = 10;
    public bool ShowPool { get; set; }
    public int PoolTagCount { get; set; } = 20;
    public bool ShowDedupSuggest { get; set; }
    public bool ShowDefragSuggest { get; set; }
    public bool RunLeakScan { get; set; }
    public int LeakScanDurationHours { get; set; } = 24;
    public int LeakScanIntervalSeconds { get; set; } = 60;
    public bool RunRealTimeMonitor { get; set; }
    public int MonitorDurationSeconds { get; set; } = 60;
    public int SamplingIntervalMs { get; set; } = 1000;
    public bool FastMode { get; set; }
    public string? OutputFile { get; set; }
}


