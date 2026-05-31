using System.Runtime.InteropServices;
using System.Text;

namespace MemoryDiagnostics.Interop;

public static partial class Win32
{
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetPerformanceInfo(out PERFORMANCE_INFORMATION pPerformanceInformation, uint cb);

    [DllImport("psapi.dll", SetLastError = true)]
    public static extern bool GetProcessMemoryInfo(IntPtr hProcess, out PROCESS_MEMORY_COUNTERS_EX ppsmemCounters, uint cb);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, int dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GlobalMemoryStatusEx(out MEMORYSTATUSEX lpBuffer);

    [DllImport("ntdll.dll")]
    public static extern int NtQuerySystemInformation(int SystemInformationClass, IntPtr SystemInformation, int SystemInformationLength, out int ReturnLength);

    [DllImport("ntdll.dll")]
    public static extern int NtQueryInformationProcess(IntPtr ProcessHandle, int ProcessInformationClass, IntPtr ProcessInformation, int ProcessInformationLength, out int ReturnLength);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenSCManager(string? lpMachineName, string? lpDatabaseName, uint dwDesiredAccess);

    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr OpenService(IntPtr hSCManager, string lpServiceName, uint dwDesiredAccess);

    [DllImport("advapi32.dll", SetLastError = true)]
    public static extern bool EnumServicesStatusEx(IntPtr hSCManager, uint infoLevel, uint serviceType, uint serviceState, IntPtr lpServices, uint cbBufSize, out uint pcbBytesNeeded, out uint lpServicesReturned, IntPtr lpResumeHandle, string? pszGroupName);

    [DllImport("psapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern uint GetProcessImageFileName(IntPtr hProcess, StringBuilder lpImageFileName, uint nSize);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern int QueryDosDevice(string lpDeviceName, StringBuilder lpTargetPath, int ucchMax);

    public const uint PROCESS_QUERY_INFORMATION = 0x0400;
    public const uint PROCESS_VM_READ = 0x0010;
    public const uint STANDARD_RIGHTS_REQUIRED = 0x000F0000;
    public const uint SC_MANAGER_CONNECT = 0x0001;
    public const uint SC_MANAGER_ENUMERATE_SERVICE = 0x0004;
    public const uint SERVICE_WIN32 = 0x00000030;
    public const uint SERVICE_DRIVER = 0x0000000B;
    public const uint SERVICE_STATE_ALL = 0x00000003;
    public const uint SERVICE_QUERY_STATUS = 0x0004;
    public const uint SERVICE_QUERY_CONFIG = 0x0001;

    public const int SystemProcessInformation = 5;
    public const int SystemMemoryListInformation = 80;
    public const int SystemPoolTagInformation = 22;
    public const int ProcessWorkingSetInformation = 19;
    public const int ProcessMemoryCountersEx = 5;

    [StructLayout(LayoutKind.Sequential)]
    public struct PERFORMANCE_INFORMATION
    {
        public uint cb;
        public IntPtr CommitTotal;
        public IntPtr CommitLimit;
        public IntPtr CommitPeak;
        public IntPtr PhysicalTotal;
        public IntPtr PhysicalAvailable;
        public IntPtr SystemCache;
        public IntPtr KernelTotal;
        public IntPtr KernelPaged;
        public IntPtr KernelNonpaged;
        public IntPtr PageSize;
        public uint HandleCount;
        public uint ProcessCount;
        public uint ThreadCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_MEMORY_COUNTERS_EX
    {
        public uint cb;
        public uint PageFaultCount;
        public IntPtr PeakWorkingSetSize;
        public IntPtr WorkingSetSize;
        public IntPtr QuotaPeakPagedPoolUsage;
        public IntPtr QuotaPagedPoolUsage;
        public IntPtr QuotaPeakNonPagedPoolUsage;
        public IntPtr QuotaNonPagedPoolUsage;
        public IntPtr PagefileUsage;
        public IntPtr PeakPagefileUsage;
        public IntPtr PrivateUsage;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORYSTATUSEX
    {
        public uint dwLength;
        public uint dwMemoryLoad;
        public ulong ullTotalPhys;
        public ulong ullAvailPhys;
        public ulong ullTotalPageFile;
        public ulong ullAvailPageFile;
        public ulong ullTotalVirtual;
        public ulong ullAvailVirtual;
        public ulong ullAvailExtendedVirtual;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SYSTEM_PROCESS_INFORMATION
    {
        public IntPtr NextEntryOffset;
        public uint NumberOfThreads;
        public long SpareLi1;
        public long SpareLi2;
        public long SpareLi3;
        public long CreateTime;
        public long UserTime;
        public long KernelTime;
        public ushort ImageNameLength;
        public ushort ImageNameMaximumLength;
        public IntPtr ImageName;
        public int BasePriority;
        public IntPtr UniqueProcessId;
        public IntPtr InheritedFromUniqueProcessId;
        public uint HandleCount;
        public uint SessionId;
        public IntPtr PageDirectoryBase;
        public IntPtr PeakVirtualSize;
        public IntPtr VirtualSize;
        public uint PageFaultCount;
        public IntPtr PeakWorkingSetSize;
        public IntPtr WorkingSetSize;
        public IntPtr QuotaPeakPagedPoolUsage;
        public IntPtr QuotaPagedPoolUsage;
        public IntPtr QuotaPeakNonPagedPoolUsage;
        public IntPtr QuotaNonPagedPoolUsage;
        public IntPtr PagefileUsage;
        public IntPtr PeakPagefileUsage;
        public IntPtr PrivatePageCount;
        public int ReadOperationCount;
        public int WriteOperationCount;
        public int OtherOperationCount;
        public int ReadTransferCount;
        public int WriteTransferCount;
        public int OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SYSTEM_POOL_TAG_INFORMATION
    {
        public uint Tag;
        public IntPtr PagedAllocs;
        public IntPtr PagedFrees;
        public IntPtr PagedUsed;
        public IntPtr NonPagedAllocs;
        public IntPtr NonPagedFrees;
        public IntPtr NonPagedUsed;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct ENUM_SERVICE_STATUS_PROCESS
    {
        public IntPtr lpServiceName;
        public IntPtr lpDisplayName;
        public SERVICE_STATUS_PROCESS ServiceStatusProcess;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SERVICE_STATUS_PROCESS
    {
        public uint dwServiceType;
        public uint dwCurrentState;
        public uint dwControlsAccepted;
        public uint dwWin32ExitCode;
        public uint dwServiceSpecificExitCode;
        public uint dwCheckPoint;
        public uint dwWaitHint;
        public uint dwProcessId;
        public uint dwServiceFlags;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MEMORY_WORKING_SET_EX_BLOCK
    {
        public ulong Flags;

        public ulong VirtualAddress => Flags & 0xFFFFFFFFFFF00000;
        public bool IsValid => (Flags & 0x1) != 0;
        public bool IsShareable => (Flags & 0x10) != 0;
        public bool IsShared => (Flags & 0x20) != 0;
        public bool IsModified => (Flags & 0x100) != 0;
    }

    public static string TagToString(uint tag)
    {
        var bytes = BitConverter.GetBytes(tag);
        var chars = new char[4];
        for (int i = 0; i < 4; i++)
        {
            chars[i] = (char)bytes[3 - i];
        }
        return new string(chars).TrimEnd('\0');
    }

    public static string GetProcessName(IntPtr hProcess)
    {
        var sb = new StringBuilder(1024);
        if (GetProcessImageFileName(hProcess, sb, (uint)sb.Capacity) > 0)
        {
            var devicePath = sb.ToString();
            var dosPath = new StringBuilder(260);
            if (QueryDosDevice(devicePath.Substring(0, 2), dosPath, dosPath.Capacity) > 0)
            {
                return Path.GetFileName(devicePath);
            }
            return Path.GetFileName(devicePath);
        }
        return "Unknown";
    }
}
