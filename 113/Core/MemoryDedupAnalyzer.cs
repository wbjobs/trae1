using System.Diagnostics;
using System.Runtime.InteropServices;
using MemoryDiagnostics.Interop;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class MemoryDedupAnalyzer
{
    private readonly MemoryAnalyzer _memoryAnalyzer;
    private readonly Dictionary<string, List<int>> _dllLoadMap = new();
    private readonly Dictionary<string, long> _dllSizeMap = new();
    private readonly List<string> _systemDlls = new()
    {
        "ntdll.dll", "kernel32.dll", "kernelbase.dll", "advapi32.dll",
        "user32.dll", "gdi32.dll", "gdi32full.dll", "msvcrt.dll",
        "ucrtbase.dll", "sechost.dll", "rpcrt4.dll", "combase.dll",
        "shell32.dll", "ole32.dll", "oleaut32.dll", "shlwapi.dll",
        "crypt32.dll", "cryptbase.dll", "ws2_32.dll", "mswsock.dll",
        "cfgmgr32.dll", "devobj.dll", "powrprof.dll", "profapi.dll",
        "userenv.dll", "win32u.dll", "ntmarta.dll", "bcrypt.dll",
        "msctf.dll", "imm32.dll", "dwmapi.dll", "uxtheme.dll",
        "d3d11.dll", "dxgi.dll", "d3d12.dll", "dxgidebug.dll"
    };

    public MemoryDedupAnalyzer(MemoryAnalyzer? memoryAnalyzer = null)
    {
        _memoryAnalyzer = memoryAnalyzer ?? new MemoryAnalyzer();
    }

    public DedupReport AnalyzeDedupOpportunities()
    {
        var report = new DedupReport();
        var processes = _memoryAnalyzer.GetAllProcessMemoryInfo();

        CollectDllInformation(processes);
        report.DuplicateDlls = AnalyzeDuplicateDlls();
        report.DuplicateFileCaches = AnalyzeFileCacheDuplicates();
        report.Suggestions = GenerateDedupSuggestions(report);
        report.TotalPotentialSavings = report.Suggestions.Sum(s => s.EstimatedSavings);
        report.DedupRatio = CalculateDedupRatio(report);
        report.PowerShellScript = GenerateOptimizationScript(report);

        return report;
    }

    private void CollectDllInformation(List<ProcessMemoryInfo> processes)
    {
        _dllLoadMap.Clear();
        _dllSizeMap.Clear();

        var systemProcess = processes.FirstOrDefault(p => p.ProcessName.Equals("System", StringComparison.OrdinalIgnoreCase));

        foreach (var process in processes.Take(200))
        {
            try
            {
                var hProcess = Win32.OpenProcess(
                    Win32.PROCESS_QUERY_INFORMATION | Win32.PROCESS_VM_READ,
                    false,
                    process.ProcessId);

                if (hProcess == IntPtr.Zero)
                    continue;

                try
                {
                    var modules = Process.GetProcessById(process.ProcessId).Modules;
                    foreach (ProcessModule module in modules)
                    {
                        var dllName = module.ModuleName!;

                        if (!_dllLoadMap.ContainsKey(dllName))
                        {
                            _dllLoadMap[dllName] = new List<int>();
                            _dllSizeMap[dllName] = module.ModuleMemorySize;
                        }

                        if (!_dllLoadMap[dllName].Contains(process.ProcessId))
                        {
                            _dllLoadMap[dllName].Add(process.ProcessId);
                        }
                    }
                }
                catch
                {
                }
                finally
                {
                    Win32.CloseHandle(hProcess);
                }
            }
            catch
            {
            }
        }
    }

    private List<DuplicateDllInfo> AnalyzeDuplicateDlls()
    {
        var result = new List<DuplicateDllInfo>();

        foreach (var (dllName, processIds) in _dllLoadMap)
        {
            if (processIds.Count < 3)
                continue;

            var dllSize = _dllSizeMap.TryGetValue(dllName, out var size) ? size : 0;
            var totalMemory = (long)processIds.Count * dllSize;
            var estimatedSavings = (long)(processIds.Count - 1) * dllSize;

            var loadingProcesses = new List<string>();
            foreach (var pid in processIds.Take(10))
            {
                try
                {
                    var proc = Process.GetProcessById(pid);
                    loadingProcesses.Add($"{proc.ProcessName} (PID: {pid})");
                }
                catch
                {
                    loadingProcesses.Add($"PID: {pid}");
                }
            }

            if (processIds.Count > 10)
            {
                loadingProcesses.Add($"... 和其他 {processIds.Count - 10} 个进程");
            }

            bool isSystemDll = _systemDlls.Any(s =>
                s.Equals(dllName, StringComparison.OrdinalIgnoreCase));

            double compressionRatio = isSystemDll ? 0.3 : 0.5;
            if (dllName.EndsWith(".exe", StringComparison.OrdinalIgnoreCase))
                compressionRatio = 0.2;

            result.Add(new DuplicateDllInfo
            {
                DllName = dllName,
                FullPath = dllName,
                FileSize = dllSize,
                LoadCount = processIds.Count,
                TotalMemoryUsed = totalMemory,
                EstimatedSavings = (long)(estimatedSavings * compressionRatio),
                LoadingProcesses = loadingProcesses,
                SuitableForCompression = processIds.Count >= 5 && dllSize >= 1024 * 1024,
                CompressionRatio = compressionRatio
            });
        }

        return result
            .Where(d => d.LoadCount >= 3)
            .OrderByDescending(d => d.EstimatedSavings)
            .ToList();
    }

    private List<FileCacheInfo> AnalyzeFileCacheDuplicates()
    {
        var result = new List<FileCacheInfo>();

        try
        {
            var perfInfo = new Win32.PERFORMANCE_INFORMATION();
            if (Win32.GetPerformanceInfo(out perfInfo, (uint)Marshal.SizeOf(perfInfo)))
            {
                var cacheSize = (long)(perfInfo.SystemCache.ToInt64() * 4096);

                result.Add(new FileCacheInfo
                {
                    FilePath = "系统文件缓存",
                    FileSize = cacheSize,
                    CachedSize = cacheSize,
                    ReferenceCount = 1,
                    EstimatedSavings = (long)(cacheSize * 0.1)
                });
            }

            result.Add(new FileCacheInfo
            {
                FilePath = "注册表缓存",
                FileSize = 0,
                CachedSize = 0,
                ReferenceCount = 1,
                EstimatedSavings = 0
            });
        }
        catch
        {
        }

        return result.OrderByDescending(f => f.EstimatedSavings).ToList();
    }

    private List<DedupSuggestion> GenerateDedupSuggestions(DedupReport report)
    {
        var suggestions = new List<DedupSuggestion>();

        foreach (var dll in report.DuplicateDlls.Take(20))
        {
            if (dll.SuitableForCompression && dll.EstimatedSavings >= 1024 * 1024 * 5)
            {
                suggestions.Add(new DedupSuggestion
                {
                    Category = "DLL去重",
                    Name = dll.DllName,
                    CurrentUsage = dll.TotalMemoryUsed,
                    EstimatedSavings = dll.EstimatedSavings,
                    Suggestion = $"启用内存压缩可节省 {MemoryFormatter.FormatBytes(dll.EstimatedSavings)}，该DLL被 {dll.LoadCount} 个进程加载",
                    Priority = dll.EstimatedSavings >= 1024 * 1024 * 50 ? 1 :
                              dll.EstimatedSavings >= 1024 * 1024 * 20 ? 2 : 3,
                    PriorityScore = (double)dll.EstimatedSavings / (1024 * 1024) * dll.LoadCount
                });
            }
        }

        suggestions.Add(new DedupSuggestion
        {
            Category = "系统优化",
            Name = "启用内存压缩",
            CurrentUsage = 0,
            EstimatedSavings = report.TotalPotentialSavings,
            Suggestion = "运行 Enable-MMAgent -MemoryCompression 启用系统内存压缩功能",
            Priority = 1,
            PriorityScore = 100
        });

        suggestions.Add(new DedupSuggestion
        {
            Category = "系统优化",
            Name = "优化文件缓存",
            CurrentUsage = (long)report.DuplicateFileCaches.Sum(f => f.CachedSize),
            EstimatedSavings = (long)report.DuplicateFileCaches.Sum(f => f.EstimatedSavings),
            Suggestion = "考虑减小系统文件缓存大小，或将不常用的文件移出缓存",
            Priority = 2,
            PriorityScore = 50
        });

        return suggestions.OrderBy(s => s.Priority).ThenByDescending(s => s.PriorityScore).ToList();
    }

    private double CalculateDedupRatio(DedupReport report)
    {
        var totalMemory = report.DuplicateDlls.Sum(d => d.TotalMemoryUsed);
        if (totalMemory == 0) return 0;

        var totalSavings = report.DuplicateDlls.Sum(d => d.EstimatedSavings);
        return (double)totalSavings / totalMemory * 100;
    }

    private string GenerateOptimizationScript(DedupReport report)
    {
        var script = new System.Text.StringBuilder();

        script.AppendLine("# Windows 内存优化脚本");
        script.AppendLine($"# 生成时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}");
        script.AppendLine($"# 预计可节省内存: {MemoryFormatter.FormatBytes(report.TotalPotentialSavings)}");
        script.AppendLine("");
        script.AppendLine("# 需要以管理员权限运行");
        script.AppendLine("#requires -RunAsAdministrator");
        script.AppendLine("");
        script.AppendLine("# 启用内存压缩");
        script.AppendLine("Write-Host '正在启用内存压缩功能...'");
        script.AppendLine("try {");
        script.AppendLine("    Enable-MMAgent -MemoryCompression");
        script.AppendLine("    Write-Host '内存压缩已启用' -ForegroundColor Green");
        script.AppendLine("} catch {");
        script.AppendLine("    Write-Host '启用内存压缩失败:' $_.Exception.Message -ForegroundColor Red");
        script.AppendLine("}");
        script.AppendLine("");
        script.AppendLine("# 优化工作集");
        script.AppendLine("Write-Host '正在优化进程工作集...'");
        script.AppendLine("try {");
        script.AppendLine("    $processes = Get-Process | Where-Object { $_.WorkingSet64 -gt 100MB }");
        script.AppendLine("    foreach ($proc in $processes) {");
        script.AppendLine("        try {");
        script.AppendLine("            $proc.MaxWorkingSet = $proc.MinWorkingSet");
        script.AppendLine("            Write-Host \"已优化: $($proc.ProcessName) (PID: $($proc.Id))\" -ForegroundColor Gray");
        script.AppendLine("        } catch {}");
        script.AppendLine("    }");
        script.AppendLine("    Write-Host '工作集优化完成' -ForegroundColor Green");
        script.AppendLine("} catch {");
        script.AppendLine("    Write-Host '工作集优化失败:' $_.Exception.Message -ForegroundColor Red");
        script.AppendLine("}");
        script.AppendLine("");
        script.AppendLine("# 清理系统缓存");
        script.AppendLine("Write-Host '正在清理系统缓存...'");
        script.AppendLine("try {");
        script.AppendLine("    [System.IO.File]::WriteAllText('\\\\?\\GLOBALROOT\\Device\\MemoryPurge', '1')");
        script.AppendLine("    Write-Host '系统缓存清理完成' -ForegroundColor Green");
        script.AppendLine("} catch {");
        script.AppendLine("    Write-Host '注意: 清理缓存需要特殊权限' -ForegroundColor Yellow");
        script.AppendLine("}");
        script.AppendLine("");
        script.AppendLine("# 内存整理建议");
        script.AppendLine("Write-Host ''");
        script.AppendLine("Write-Host '=== 内存优化建议 ===' -ForegroundColor Cyan");

        foreach (var dll in report.DuplicateDlls.Take(10))
        {
            script.AppendLine($"Write-Host '\"{dll.DllName}\" 被 {dll.LoadCount} 个进程加载, 可节省: {MemoryFormatter.FormatBytes(dll.EstimatedSavings)}'");
        }

        script.AppendLine("");
        script.AppendLine("Write-Host ''");
        script.AppendLine($"Write-Host '总计可节省内存: {MemoryFormatter.FormatBytes(report.TotalPotentialSavings)}' -ForegroundColor Green");

        return script.ToString();
    }

    public void PrintDedupReport(DedupReport report)
    {
        Console.WriteLine();
        Console.WriteLine("=== 内存去重分析报告 ===");
        Console.WriteLine($"时间: {report.Timestamp:yyyy-MM-dd HH:mm:ss}");
        Console.WriteLine();

        Console.WriteLine("重复DLL分析 (加载次数>=3):");
        Console.WriteLine($"{"DLL名称",30} {"加载次数",10} {"总大小",15} {"可节省",15} {"适合压缩",8}");
        Console.WriteLine(new string('-', 85));

        foreach (var dll in report.DuplicateDlls.Take(15))
        {
            var suitable = dll.SuitableForCompression ? "是" : "否";
            var color = dll.SuitableForCompression ? ConsoleColor.Green : ConsoleColor.Gray;

            Console.ForegroundColor = color;
            Console.WriteLine($"{Truncate(dll.DllName, 30),30} {dll.LoadCount,10} " +
                            $"{MemoryFormatter.FormatBytes(dll.TotalMemoryUsed),15} " +
                            $"{MemoryFormatter.FormatBytes(dll.EstimatedSavings),15} " +
                            $"{suitable,8}");
            Console.ResetColor();
        }

        Console.WriteLine();
        Console.WriteLine($"去重比率: {report.DedupRatio:F1}%");
        Console.WriteLine($"预计可节省内存: {MemoryFormatter.FormatBytes(report.TotalPotentialSavings)}");
        Console.WriteLine();

        Console.WriteLine("=== 优化建议 ===");
        foreach (var suggestion in report.Suggestions.Take(10))
        {
            var priorityColor = suggestion.Priority switch
            {
                1 => ConsoleColor.Red,
                2 => ConsoleColor.Yellow,
                _ => ConsoleColor.White
            };

            Console.ForegroundColor = priorityColor;
            Console.WriteLine($"[优先级 {suggestion.Priority}] {suggestion.Category}: {suggestion.Name}");
            Console.ResetColor();
            Console.WriteLine($"  {suggestion.Suggestion}");
            Console.WriteLine($"  当前使用: {MemoryFormatter.FormatBytes(suggestion.CurrentUsage)} | 可节省: {MemoryFormatter.FormatBytes(suggestion.EstimatedSavings)}");
            Console.WriteLine();
        }

        Console.WriteLine("PowerShell优化脚本已生成，可使用 --output 导出。");
    }

    private string Truncate(string s, int maxLength)
    {
        if (string.IsNullOrEmpty(s)) return s;
        return s.Length <= maxLength ? s : s.Substring(0, maxLength - 3) + "...";
    }
}
