namespace MemoryDiagnostics.Models;

public record DuplicateDllInfo
{
    public string DllName { get; set; } = string.Empty;
    public string FullPath { get; set; } = string.Empty;
    public long FileSize { get; set; }
    public int LoadCount { get; set; }
    public long TotalMemoryUsed { get; set; }
    public long EstimatedSavings { get; set; }
    public List<string> LoadingProcesses { get; set; } = new();
    public bool SuitableForCompression { get; set; }
    public double CompressionRatio { get; set; }
}

public record FileCacheInfo
{
    public string FilePath { get; set; } = string.Empty;
    public long FileSize { get; set; }
    public long CachedSize { get; set; }
    public int ReferenceCount { get; set; }
    public long EstimatedSavings { get; set; }
}

public record DedupSuggestion
{
    public string Category { get; set; } = string.Empty;
    public string Name { get; set; } = string.Empty;
    public long CurrentUsage { get; set; }
    public long EstimatedSavings { get; set; }
    public string Suggestion { get; set; } = string.Empty;
    public int Priority { get; set; }
    public double PriorityScore { get; set; }
}

public record DedupReport
{
    public DateTime Timestamp { get; set; } = DateTime.Now;
    public List<DuplicateDllInfo> DuplicateDlls { get; set; } = new();
    public List<FileCacheInfo> DuplicateFileCaches { get; set; } = new();
    public List<DedupSuggestion> Suggestions { get; set; } = new();
    public long TotalPotentialSavings { get; set; }
    public double DedupRatio { get; set; }
    public string PowerShellScript { get; set; } = string.Empty;
}

public record MemoryFragmentInfo
{
    public long FreeBlocksCount { get; set; }
    public long FreeBlocksTotalSize { get; set; }
    public long LargestFreeBlock { get; set; }
    public long SmallestFreeBlock { get; set; }
    public double AverageFreeBlockSize { get; set; }
    public double FragmentationIndex { get; set; }
    public long FreeBlocksSmall { get; set; }
    public long FreeBlocksMedium { get; set; }
    public long FreeBlocksLarge { get; set; }
    public long FreeBlocksHuge { get; set; }
}

public record DefragSuggestion
{
    public string Issue { get; set; } = string.Empty;
    public string Impact { get; set; } = string.Empty;
    public string Suggestion { get; set; } = string.Empty;
    public int Priority { get; set; }
}

public record DefragReport
{
    public DateTime Timestamp { get; set; } = DateTime.Now;
    public MemoryFragmentInfo FragmentInfo { get; set; } = new();
    public List<DefragSuggestion> Suggestions { get; set; } = new();
    public string PowerShellScript { get; set; } = string.Empty;
    public string OverallStatus { get; set; } = string.Empty;
}

public record OptimizationReport
{
    public DateTime Timestamp { get; set; } = DateTime.Now;
    public PhysicalMemoryDistribution BeforeOptimization { get; set; } = new();
    public PhysicalMemoryDistribution AfterOptimization { get; set; } = new();
    public DedupReport? DedupAnalysis { get; set; }
    public DefragReport? DefragAnalysis { get; set; }
    public long EstimatedTotalSavings { get; set; }
    public string Summary { get; set; } = string.Empty;
    public string HtmlReport { get; set; } = string.Empty;
}
