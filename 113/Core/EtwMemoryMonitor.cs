using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using MemoryDiagnostics.Models;
using Microsoft.Diagnostics.Tracing;
using Microsoft.Diagnostics.Tracing.Parsers;
using Microsoft.Diagnostics.Tracing.Session;

namespace MemoryDiagnostics.Core;

public class EtwMemoryMonitor : IDisposable
{
    private TraceEventSession? _session;
    private readonly BlockingCollection<EtwMemoryEvent> _eventQueue = new();
    private CancellationTokenSource? _cts;
    private Task? _monitorTask;
    private Task? _processingTask;
    private readonly int _samplingIntervalMs;
    private readonly MemoryAnalyzer _analyzer;
    private bool _disposed;

    public event Action<EtwMemoryEvent>? OnMemoryEvent;
    public event Action<MemorySample>? OnSampleCollected;

    public bool FastMode
    {
        get => _analyzer.FastMode;
        set => _analyzer.FastMode = value;
    }

    public EtwMemoryMonitor(int samplingIntervalMs = 1000, bool fastMode = false)
    {
        _samplingIntervalMs = samplingIntervalMs;
        _analyzer = new MemoryAnalyzer { FastMode = fastMode };
    }

    public async Task StartMonitoring(CancellationToken cancellationToken = default)
    {
        if (_monitorTask != null && !_monitorTask.IsCompleted)
        {
            throw new InvalidOperationException("监控已在运行中");
        }

        _cts = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        var token = _cts.Token;

        StartEtwSession();

        _monitorTask = Task.Run(async () =>
        {
            try
            {
                while (!token.IsCancellationRequested)
                {
                    try
                    {
                        var sample = CollectMemorySample();
                        OnSampleCollected?.Invoke(sample);
                    }
                    catch (Exception ex)
                    {
                        Console.Error.WriteLine($"采集样本时出错: {ex.Message}");
                    }

                    await Task.Delay(_samplingIntervalMs, token);
                }
            }
            catch (OperationCanceledException)
            {
            }
            finally
            {
                StopEtwSession();
            }
        }, token);

        _processingTask = Task.Run(() =>
        {
            try
            {
                foreach (var ev in _eventQueue.GetConsumingEnumerable(token))
                {
                    try
                    {
                        OnMemoryEvent?.Invoke(ev);
                    }
                    catch
                    {
                    }
                }
            }
            catch (OperationCanceledException)
            {
            }
        }, token);

        await Task.CompletedTask;
    }

    private void StartEtwSession()
    {
        try
        {
            string sessionName = $"MemoryDiagnostics_{Process.GetCurrentProcess().Id}";
            _session = new TraceEventSession(sessionName, TraceEventSessionOptions.Create);

            _session.EnableKernelProvider(
                KernelTraceEventParser.Keywords.VirtualAlloc |
                KernelTraceEventParser.Keywords.Memory |
                KernelTraceEventParser.Keywords.Process |
                KernelTraceEventParser.Keywords.Pool |
                KernelTraceEventParser.Keywords.FileIOInit);

            var kernelParser = _session.Source.Kernel;

            kernelParser.VirtualAlloc += data =>
            {
                if (data.ProviderGuid != Guid.Empty)
                {
                    _eventQueue.Add(new EtwMemoryEvent
                    {
                        Timestamp = data.TimeStamp,
                        ProcessId = data.ProcessID,
                        EventType = "VirtualAlloc",
                        AllocationSize = (long)data.Size,
                        Address = data.BaseAddr
                    });
                }
            };

            kernelParser.VirtualFree += data =>
            {
                _eventQueue.Add(new EtwMemoryEvent
                {
                    Timestamp = data.TimeStamp,
                    ProcessId = data.ProcessID,
                    EventType = "VirtualFree",
                    AllocationSize = -(long)data.Size,
                    Address = data.BaseAddr
                });
            };

            kernelParser.MapFile += data =>
            {
                _eventQueue.Add(new EtwMemoryEvent
                {
                    Timestamp = data.TimeStamp,
                    ProcessId = data.ProcessID,
                    EventType = "MapFile",
                    AllocationSize = (long)data.ViewSize,
                    Address = data.ViewBase
                });
            };

            kernelParser.UnmapFile += data =>
            {
                _eventQueue.Add(new EtwMemoryEvent
                {
                    Timestamp = data.TimeStamp,
                    ProcessId = data.ProcessID,
                    EventType = "UnmapFile",
                    AllocationSize = -(long)data.ViewSize,
                    Address = data.ViewBase
                });
            };

            _session.Source.Process();
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine($"警告: 启动ETW会话失败（需要管理员权限）: {ex.Message}");
            Console.Error.WriteLine("将仅使用轮询模式进行内存监控。");
        }
    }

    private void StopEtwSession()
    {
        try
        {
            if (_session != null)
            {
                _session.Source.StopProcessing();
                _session.Dispose();
                _session = null;
            }
        }
        catch
        {
        }
    }

    private MemorySample CollectMemorySample()
    {
        var sample = new MemorySample
        {
            Timestamp = DateTime.Now,
            SystemMemory = _analyzer.GetPhysicalMemoryDistribution(),
            Processes = _analyzer.GetAllProcessMemoryInfo(),
            PoolTags = _analyzer.GetPoolTagInformation()
        };

        return sample;
    }

    public async Task StopMonitoring()
    {
        if (_cts != null)
        {
            _cts.Cancel();
            try
            {
                if (_monitorTask != null)
                {
                    await _monitorTask;
                }
                if (_processingTask != null)
                {
                    await _processingTask;
                }
            }
            catch (OperationCanceledException)
            {
            }
            finally
            {
                _cts.Dispose();
                _cts = null;
                _monitorTask = null;
                _processingTask = null;
            }
        }

        StopEtwSession();
        _eventQueue.CompleteAdding();
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!_disposed)
        {
            if (disposing)
            {
                _ = StopMonitoring().GetAwaiter().GetResult();
                _eventQueue.Dispose();
                _analyzer.ClearCache();
            }
            _disposed = true;
        }
    }

    public void Dispose()
    {
        Dispose(true);
        GC.SuppressFinalize(this);
    }

    ~EtwMemoryMonitor()
    {
        Dispose(false);
    }
}
