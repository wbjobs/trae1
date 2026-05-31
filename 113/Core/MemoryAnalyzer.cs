using System.Runtime.InteropServices;
using MemoryDiagnostics.Interop;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class MemoryAnalyzer
{
    private static readonly uint PageSize;
    private readonly Dictionary<int, string> _processNameCache = new();
    private List<ProcessMemoryInfo>? _cachedProcessList;
    private DateTime _cacheExpiry = DateTime.MinValue;
    private readonly int _cacheDurationMs = 500;
    private bool _fastMode = false;

    public bool FastMode
    {
        get => _fastMode;
        set => _fastMode = value;
    }

    static MemoryAnalyzer()
    {
        var perfInfo = new Win32.PERFORMANCE_INFORMATION();
        if (Win32.GetPerformanceInfo(out perfInfo, (uint)Marshal.SizeOf(perfInfo)))
        {
            PageSize = (uint)perfInfo.PageSize.ToInt64();
        }
        else
        {
            PageSize = 4096;
        }
    }

    public PhysicalMemoryDistribution GetPhysicalMemoryDistribution()
    {
        var result = new PhysicalMemoryDistribution();

        try
        {
            var perfInfo = new Win32.PERFORMANCE_INFORMATION();
            if (Win32.GetPerformanceInfo(out perfInfo, (uint)Marshal.SizeOf(perfInfo)))
            {
                result.TotalPhysical = (ulong)(perfInfo.PhysicalTotal.ToInt64() * PageSize);
                result.FreeMemory = (ulong)(perfInfo.PhysicalAvailable.ToInt64() * PageSize);
                result.SystemCache = (ulong)(perfInfo.SystemCache.ToInt64() * PageSize);
                result.PagedPool = (ulong)(perfInfo.KernelPaged.ToInt64() * PageSize);
                result.NonPagedPool = (ulong)(perfInfo.KernelNonpaged.ToInt64() * PageSize);
            }

            var memoryStatus = new Win32.MEMORYSTATUSEX { dwLength = (uint)Marshal.SizeOf<Win32.MEMORYSTATUSEX>() };
            if (Win32.GlobalMemoryStatusEx(out memoryStatus))
            {
                result.TotalPhysical = memoryStatus.ullTotalPhys;
                result.FreeMemory = memoryStatus.ullAvailPhys;
            }

            if (_fastMode)
            {
                result.ProcessPrivate = 0;
                result.SharedMemory = 0;
                result.DriverLocked = 0;
                return result;
            }

            ulong processPrivate = 0;
            ulong sharedMemory = 0;

            var processes = GetAllProcessMemoryInfo();
            foreach (var proc in processes)
            {
                processPrivate += (ulong)Math.Max(0, proc.PrivateWorkingSet);
                sharedMemory += (ulong)Math.Max(0, proc.SharedWorkingSet);
            }

            result.ProcessPrivate = processPrivate;
            result.SharedMemory = sharedMemory;

            ulong driverLocked = 0;
            try
            {
                var poolTags = GetPoolTagInformation();
                driverLocked = (ulong)poolTags.Sum(t => t.NonPagedUsed + t.PagedUsed);
            }
            catch
            {
                driverLocked = 0;
            }

            if (result.TotalPhysical > 0)
            {
                var accounted = result.ProcessPrivate + result.SharedMemory + result.PagedPool +
                              result.NonPagedPool + result.SystemCache + driverLocked + result.FreeMemory;

                if (accounted < result.TotalPhysical)
                {
                    driverLocked += result.TotalPhysical - accounted;
                }
            }

            result.DriverLocked = driverLocked;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"警告: 获取内存分布时出错 - {ex.Message}");
        }

        return result;
    }

    public List<ProcessMemoryInfo> GetAllProcessMemoryInfo()
    {
        if (_cachedProcessList != null && DateTime.Now < _cacheExpiry)
        {
            return _cachedProcessList;
        }

        var result = new List<ProcessMemoryInfo>();
        int bufferSize = 1024 * 1024 * 32;
        IntPtr buffer = Marshal.AllocHGlobal(bufferSize);

        try
        {
            int returnLength;
            int status = Win32.NtQuerySystemInformation(
                Win32.SystemProcessInformation,
                buffer,
                bufferSize,
                out returnLength);

            if (status != 0 && returnLength > bufferSize)
            {
                Marshal.FreeHGlobal(buffer);
                bufferSize = returnLength + 4096;
                buffer = Marshal.AllocHGlobal(bufferSize);
                status = Win32.NtQuerySystemInformation(
                    Win32.SystemProcessInformation,
                    buffer,
                    bufferSize,
                    out returnLength);
            }

            if (status == 0)
            {
                long currentPtr = buffer.ToInt64();
                int entrySize = Marshal.SizeOf<Win32.SYSTEM_PROCESS_INFORMATION>();

                while (true)
                {
                    var spi = Marshal.PtrToStructure<Win32.SYSTEM_PROCESS_INFORMATION>(new IntPtr(currentPtr));

                    if (spi.UniqueProcessId != IntPtr.Zero)
                    {
                        int pid = spi.UniqueProcessId.ToInt32();
                        string processName;

                        if (!_processNameCache.TryGetValue(pid, out processName!))
                        {
                            if (spi.ImageName != IntPtr.Zero)
                            {
                                try
                                {
                                    processName = Marshal.PtrToStringUni(spi.ImageName, spi.ImageNameLength / 2);
                                    processName = Path.GetFileNameWithoutExtension(processName);
                                }
                                catch
                                {
                                    processName = $"Process_{pid}";
                                }
                            }
                            else
                            {
                                processName = $"Process_{pid}";
                            }
                            _processNameCache[pid] = processName;
                        }

                        long workingSet = spi.WorkingSetSize.ToInt64();
                        long privateBytes = spi.PrivatePageCount.ToInt64() * PageSize;
                        long pagedPool = spi.QuotaPagedPoolUsage.ToInt64();
                        long nonPagedPool = spi.QuotaNonPagedPoolUsage.ToInt64();

                        long sharedWorkingSet = 0;
                        if (!_fastMode && workingSet > privateBytes)
                        {
                            sharedWorkingSet = workingSet - privateBytes;
                        }

                        result.Add(new ProcessMemoryInfo
                        {
                            ProcessId = pid,
                            ProcessName = processName,
                            WorkingSetSize = workingSet,
                            PrivateWorkingSet = privateBytes,
                            SharedWorkingSet = sharedWorkingSet,
                            PrivateBytes = privateBytes,
                            PageFileUsage = spi.PagefileUsage.ToInt64(),
                            PagedPoolUsage = pagedPool,
                            NonPagedPoolUsage = nonPagedPool,
                            HandleCount = spi.HandleCount,
                            ThreadCount = spi.NumberOfThreads
                        });
                    }

                    if (spi.NextEntryOffset == IntPtr.Zero)
                    {
                        break;
                    }
                    currentPtr += spi.NextEntryOffset.ToInt64();
                }
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }

        result = result.OrderByDescending(p => p.WorkingSetSize).ToList();

        _cachedProcessList = result;
        _cacheExpiry = DateTime.Now.AddMilliseconds(_cacheDurationMs);

        return result;
    }

    public ProcessMemoryInfo? GetProcessMemoryInfo(int processId)
    {
        return GetAllProcessMemoryInfo().FirstOrDefault(p => p.ProcessId == processId);
    }

    public List<PoolTagEntry> GetPoolTagInformation()
    {
        var result = new List<PoolTagEntry>();

        if (_fastMode)
        {
            return result;
        }

        int bufferSize = 1024 * 1024 * 16;
        IntPtr buffer = Marshal.AllocHGlobal(bufferSize);

        try
        {
            int returnLength;
            int status = Win32.NtQuerySystemInformation(
                Win32.SystemPoolTagInformation,
                buffer,
                bufferSize,
                out returnLength);

            if (status == 0 && returnLength > 0)
            {
                int count = Marshal.ReadInt32(buffer);
                int entrySize = Marshal.SizeOf<Win32.SYSTEM_POOL_TAG_INFORMATION>();
                long entriesPtr = buffer.ToInt64() + IntPtr.Size;

                for (int i = 0; i < count; i++)
                {
                    var tagInfo = Marshal.PtrToStructure<Win32.SYSTEM_POOL_TAG_INFORMATION>(
                        new IntPtr(entriesPtr + i * entrySize));

                    if (tagInfo.PagedUsed.ToInt64() > 0 || tagInfo.NonPagedUsed.ToInt64() > 0)
                    {
                        var tagStr = Win32.TagToString(tagInfo.Tag);
                        result.Add(new PoolTagEntry
                        {
                            Tag = $"0x{tagInfo.Tag:X8}",
                            TagName = tagStr,
                            PagedUsed = tagInfo.PagedUsed.ToInt64(),
                            NonPagedUsed = tagInfo.NonPagedUsed.ToInt64(),
                            PagedAllocs = tagInfo.PagedAllocs.ToInt64(),
                            PagedFrees = tagInfo.PagedFrees.ToInt64(),
                            NonPagedAllocs = tagInfo.NonPagedAllocs.ToInt64(),
                            NonPagedFrees = tagInfo.NonPagedFrees.ToInt64()
                        });
                    }
                }
            }
        }
        finally
        {
            Marshal.FreeHGlobal(buffer);
        }

        return result.OrderByDescending(t => t.NonPagedUsed + t.PagedUsed).ToList();
    }

    public List<ProcessMemoryInfo> GetTopProcessesByMemory(int topN = 10)
    {
        return GetAllProcessMemoryInfo()
            .Take(topN)
            .ToList();
    }

    public void ClearCache()
    {
        _processNameCache.Clear();
        _cachedProcessList = null;
        _cacheExpiry = DateTime.MinValue;
    }
}
