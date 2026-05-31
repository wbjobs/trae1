using System.Text;
using MemoryDiagnostics.Models;

namespace MemoryDiagnostics.Core;

public class HtmlReportGenerator
{
    public string GenerateOptimizationReport(
        PhysicalMemoryDistribution beforeMemory,
        DedupReport? dedupReport,
        DefragReport? defragReport,
        long estimatedSavings)
    {
        var afterMemory = new PhysicalMemoryDistribution
        {
            TotalPhysical = beforeMemory.TotalPhysical,
            FreeMemory = beforeMemory.FreeMemory + (ulong)estimatedSavings,
            ProcessPrivate = beforeMemory.ProcessPrivate,
            SharedMemory = beforeMemory.SharedMemory,
            PagedPool = beforeMemory.PagedPool,
            NonPagedPool = beforeMemory.NonPagedPool,
            SystemCache = beforeMemory.SystemCache,
            DriverLocked = beforeMemory.DriverLocked,
            Timestamp = DateTime.Now
        };

        var sb = new StringBuilder();

        sb.AppendLine("<!DOCTYPE html>");
        sb.AppendLine("<html lang=\"zh-CN\">");
        sb.AppendLine("<head>");
        sb.AppendLine("<meta charset=\"UTF-8\">");
        sb.AppendLine("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">");
        sb.AppendLine("<title>Windows 内存优化报告</title>");
        sb.AppendLine("<style>");
        sb.AppendLine(GenerateCss());
        sb.AppendLine("</style>");
        sb.AppendLine("</head>");
        sb.AppendLine("<body>");

        sb.AppendLine("<div class=\"container\">");
        sb.AppendLine("<header class=\"header\">");
        sb.AppendLine("<h1>Windows 内存优化分析报告</h1>");
        sb.AppendLine($"<p class=\"timestamp\">生成时间: {DateTime.Now:yyyy-MM-dd HH:mm:ss}</p>");
        sb.AppendLine("</header>");

        sb.AppendLine("<section class=\"summary-section\">");
        sb.AppendLine("<h2>优化概要</h2>");
        sb.AppendLine("<div class=\"summary-cards\">");

        sb.AppendLine("<div class=\"card\">");
        sb.AppendLine("<div class=\"card-title\">总物理内存</div>");
        sb.AppendLine($"<div class=\"card-value\">{MemoryFormatter.FormatBytes((long)beforeMemory.TotalPhysical)}</div>");
        sb.AppendLine("</div>");

        sb.AppendLine("<div class=\"card\">");
        sb.AppendLine("<div class=\"card-title\">优化前空闲</div>");
        sb.AppendLine($"<div class=\"card-value warning\">{MemoryFormatter.FormatBytes((long)beforeMemory.FreeMemory)}</div>");
        sb.AppendLine("</div>");

        sb.AppendLine("<div class=\"card\">");
        sb.AppendLine("<div class=\"card-title\">优化后空闲</div>");
        sb.AppendLine($"<div class=\"card-value success\">{MemoryFormatter.FormatBytes((long)afterMemory.FreeMemory)}</div>");
        sb.AppendLine("</div>");

        sb.AppendLine("<div class=\"card\">");
        sb.AppendLine("<div class=\"card-title\">预计节省</div>");
        sb.AppendLine($"<div class=\"card-value highlight\">+{MemoryFormatter.FormatBytes(estimatedSavings)}</div>");
        sb.AppendLine("</div>");

        sb.AppendLine("</div>");
        sb.AppendLine("</section>");

        sb.AppendLine("<section class=\"chart-section\">");
        sb.AppendLine("<h2>内存使用对比</h2>");
        sb.AppendLine("<div class=\"chart-container\">");
        sb.AppendLine(GenerateMemoryBarChart(beforeMemory, afterMemory));
        sb.AppendLine("</div>");
        sb.AppendLine("</section>");

        if (dedupReport != null && dedupReport.DuplicateDlls.Count > 0)
        {
            sb.AppendLine("<section class=\"dedup-section\">");
            sb.AppendLine("<h2>重复DLL分析</h2>");
            sb.AppendLine($"<p class=\"section-desc\">发现 {dedupReport.DuplicateDlls.Count} 个被多进程加载的DLL，去重比率: {dedupReport.DedupRatio:F1}%</p>");
            sb.AppendLine("<table class=\"data-table\">");
            sb.AppendLine("<thead>");
            sb.AppendLine("<tr><th>DLL名称</th><th>加载次数</th><th>总大小</th><th>可节省</th><th>适合压缩</th></tr>");
            sb.AppendLine("</thead>");
            sb.AppendLine("<tbody>");

            foreach (var dll in dedupReport.DuplicateDlls.Take(15))
            {
                var suitableClass = dll.SuitableForCompression ? "yes" : "no";
                var suitableText = dll.SuitableForCompression ? "是" : "否";
                sb.AppendLine($"<tr>");
                sb.AppendLine($"<td>{dll.DllName}</td>");
                sb.AppendLine($"<td class=\"center\">{dll.LoadCount}</td>");
                sb.AppendLine($"<td class=\"right\">{MemoryFormatter.FormatBytes(dll.TotalMemoryUsed)}</td>");
                sb.AppendLine($"<td class=\"right highlight\">{MemoryFormatter.FormatBytes(dll.EstimatedSavings)}</td>");
                sb.AppendLine($"<td class=\"center {suitableClass}\">{suitableText}</td>");
                sb.AppendLine($"</tr>");
            }

            sb.AppendLine("</tbody>");
            sb.AppendLine("</table>");
            sb.AppendLine("</section>");
        }

        if (defragReport != null)
        {
            sb.AppendLine("<section class=\"defrag-section\">");
            sb.AppendLine("<h2>内存碎片分析</h2>");

            var fragColor = defragReport.FragmentInfo.FragmentationIndex >= 70 ? "high" :
                           defragReport.FragmentInfo.FragmentationIndex >= 40 ? "medium" : "low";

            sb.AppendLine("<div class=\"fragmentation-summary\">");
            sb.AppendLine($"<div class=\"fragmentation-index {fragColor}\">");
            sb.AppendLine("<div class=\"fragmentation-label\">碎片指数</div>");
            sb.AppendLine($"<div class=\"fragmentation-value\">{defragReport.FragmentInfo.FragmentationIndex:F0}%</div>");
            sb.AppendLine($"<div class=\"fragmentation-status\">{defragReport.OverallStatus}</div>");
            sb.AppendLine("</div>");

            sb.AppendLine("<div class=\"fragmentation-details\">");
            sb.AppendLine("<div class=\"detail-item\">");
            sb.AppendLine($"<span class=\"detail-label\">空闲块数:</span>");
            sb.AppendLine($"<span class=\"detail-value\">{defragReport.FragmentInfo.FreeBlocksCount:N0}</span>");
            sb.AppendLine("</div>");
            sb.AppendLine("<div class=\"detail-item\">");
            sb.AppendLine($"<span class=\"detail-label\">平均块大小:</span>");
            sb.AppendLine($"<span class=\"detail-value\">{MemoryFormatter.FormatBytes((long)defragReport.FragmentInfo.AverageFreeBlockSize)}</span>");
            sb.AppendLine("</div>");
            sb.AppendLine("<div class=\"detail-item\">");
            sb.AppendLine($"<span class=\"detail-label\">最大块:</span>");
            sb.AppendLine($"<span class=\"detail-value\">{MemoryFormatter.FormatBytes(defragReport.FragmentInfo.LargestFreeBlock)}</span>");
            sb.AppendLine("</div>");
            sb.AppendLine("</div>");
            sb.AppendLine("</div>");

            if (defragReport.Suggestions.Count > 0)
            {
                sb.AppendLine("<h3>优化建议</h3>");
                sb.AppendLine("<ul class=\"suggestions-list\">");
                foreach (var suggestion in defragReport.Suggestions)
                {
                    var priorityClass = suggestion.Priority switch
                    {
                        1 => "priority-high",
                        2 => "priority-medium",
                        _ => "priority-low"
                    };
                    sb.AppendLine($"<li class=\"{priorityClass}\">");
                    sb.AppendLine($"<strong>[{suggestion.Priority}]</strong> {suggestion.Issue}");
                    sb.AppendLine($"<p>{suggestion.Suggestion}</p>");
                    sb.AppendLine("</li>");
                }
                sb.AppendLine("</ul>");
            }

            sb.AppendLine("</section>");
        }

        if (dedupReport != null && !string.IsNullOrEmpty(dedupReport.PowerShellScript))
        {
            sb.AppendLine("<section class=\"script-section\">");
            sb.AppendLine("<h2>PowerShell 优化脚本</h2>");
            sb.AppendLine("<p class=\"section-desc\">以管理员身份运行此脚本以应用优化建议</p>");
            sb.AppendLine("<pre class=\"code-block\"><code>");
            sb.AppendLine(System.Net.WebUtility.HtmlEncode(dedupReport.PowerShellScript));
            sb.AppendLine("</code></pre>");
            sb.AppendLine("</section>");
        }

        sb.AppendLine("<footer class=\"footer\">");
        sb.AppendLine("<p>Windows 内存诊断工具 - 内存优化分析报告</p>");
        sb.AppendLine("</footer>");

        sb.AppendLine("</div>");
        sb.AppendLine("</body>");
        sb.AppendLine("</html>");

        return sb.ToString();
    }

    private string GenerateCss()
    {
        return @"
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    padding: 20px;
}

.container {
    max-width: 1200px;
    margin: 0 auto;
}

.header {
    background: white;
    border-radius: 15px;
    padding: 30px;
    margin-bottom: 20px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.2);
    text-align: center;
}

.header h1 {
    color: #333;
    font-size: 2em;
    margin-bottom: 10px;
}

.timestamp {
    color: #666;
    font-size: 0.9em;
}

.summary-section {
    margin-bottom: 20px;
}

.summary-section h2, section h2 {
    color: white;
    margin-bottom: 15px;
    text-shadow: 1px 1px 2px rgba(0,0,0,0.3);
}

.summary-cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 15px;
}

.card {
    background: white;
    border-radius: 10px;
    padding: 20px;
    text-align: center;
    box-shadow: 0 5px 15px rgba(0,0,0,0.1);
    transition: transform 0.3s ease;
}

.card:hover {
    transform: translateY(-5px);
}

.card-title {
    color: #666;
    font-size: 0.9em;
    margin-bottom: 10px;
}

.card-value {
    font-size: 1.5em;
    font-weight: bold;
    color: #333;
}

.card-value.warning {
    color: #f39c12;
}

.card-value.success {
    color: #27ae60;
}

.card-value.highlight {
    color: #3498db;
}

.chart-section {
    background: white;
    border-radius: 15px;
    padding: 30px;
    margin-bottom: 20px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.2);
}

.chart-section h2 {
    color: #333;
    text-shadow: none;
}

.chart-container {
    margin-top: 20px;
}

.bar-chart {
    display: flex;
    flex-direction: column;
    gap: 15px;
}

.bar-row {
    display: grid;
    grid-template-columns: 150px 1fr 150px 1fr;
    align-items: center;
    gap: 10px;
}

.bar-label {
    font-weight: 500;
    color: #555;
}

.bar-track {
    background: #eee;
    border-radius: 10px;
    height: 30px;
    overflow: hidden;
    position: relative;
}

.bar-fill {
    height: 100%;
    border-radius: 10px;
    transition: width 0.5s ease;
    display: flex;
    align-items: center;
    justify-content: flex-end;
    padding-right: 10px;
    color: white;
    font-size: 0.8em;
    font-weight: 500;
}

.bar-fill.before {
    background: linear-gradient(90deg, #e74c3c, #c0392b);
}

.bar-fill.after {
    background: linear-gradient(90deg, #27ae60, #2ecc71);
}

.bar-value {
    font-weight: 500;
    color: #333;
    text-align: right;
}

.dedup-section, .defrag-section, .script-section {
    background: white;
    border-radius: 15px;
    padding: 30px;
    margin-bottom: 20px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.2);
}

.dedup-section h2, .defrag-section h2, .script-section h2 {
    color: #333;
    text-shadow: none;
}

.section-desc {
    color: #666;
    margin-bottom: 20px;
}

.data-table {
    width: 100%;
    border-collapse: collapse;
    margin-top: 15px;
}

.data-table th {
    background: #3498db;
    color: white;
    padding: 12px 15px;
    text-align: left;
    font-weight: 500;
}

.data-table td {
    padding: 12px 15px;
    border-bottom: 1px solid #eee;
}

.data-table tbody tr:hover {
    background: #f8f9fa;
}

.data-table .center {
    text-align: center;
}

.data-table .right {
    text-align: right;
}

.data-table .highlight {
    color: #27ae60;
    font-weight: 500;
}

.data-table .yes {
    color: #27ae60;
    font-weight: 500;
}

.data-table .no {
    color: #e74c3c;
}

.fragmentation-summary {
    display: grid;
    grid-template-columns: 200px 1fr;
    gap: 30px;
    margin-top: 20px;
}

.fragmentation-index {
    text-align: center;
    padding: 30px;
    border-radius: 15px;
    background: #f8f9fa;
}

.fragmentation-index.high {
    background: linear-gradient(135deg, #ff6b6b, #ee5a5a);
    color: white;
}

.fragmentation-index.medium {
    background: linear-gradient(135deg, #feca57, #ff9f43);
    color: white;
}

.fragmentation-index.low {
    background: linear-gradient(135deg, #48dbfb, #0abde3);
    color: white;
}

.fragmentation-label {
    font-size: 0.9em;
    opacity: 0.9;
    margin-bottom: 10px;
}

.fragmentation-value {
    font-size: 3em;
    font-weight: bold;
    margin-bottom: 10px;
}

.fragmentation-status {
    font-size: 0.85em;
    opacity: 0.9;
}

.fragmentation-details {
    display: flex;
    flex-direction: column;
    gap: 15px;
    justify-content: center;
}

.detail-item {
    display: flex;
    justify-content: space-between;
    padding: 12px;
    background: #f8f9fa;
    border-radius: 8px;
}

.detail-label {
    color: #666;
}

.detail-value {
    font-weight: 500;
    color: #333;
}

.suggestions-list {
    list-style: none;
    margin-top: 20px;
}

.suggestions-list li {
    padding: 15px;
    margin-bottom: 10px;
    border-radius: 8px;
    border-left: 4px solid;
}

.suggestions-list li.priority-high {
    background: #fde8e8;
    border-color: #e74c3c;
}

.suggestions-list li.priority-medium {
    background: #fef5e7;
    border-color: #f39c12;
}

.suggestions-list li.priority-low {
    background: #e8f8f0;
    border-color: #27ae60;
}

.suggestions-list li p {
    margin-top: 8px;
    color: #666;
    font-size: 0.9em;
}

.code-block {
    background: #282c34;
    color: #abb2bf;
    padding: 20px;
    border-radius: 8px;
    overflow-x: auto;
    font-family: 'Consolas', 'Monaco', monospace;
    font-size: 0.9em;
    line-height: 1.5;
    max-height: 400px;
    overflow-y: auto;
}

.footer {
    text-align: center;
    padding: 20px;
    color: rgba(255,255,255,0.8);
    font-size: 0.9em;
}

@media (max-width: 768px) {
    .summary-cards {
        grid-template-columns: repeat(2, 1fr);
    }
    
    .bar-row {
        grid-template-columns: 100px 1fr 100px 1fr;
        font-size: 0.85em;
    }
    
    .fragmentation-summary {
        grid-template-columns: 1fr;
    }
    
    .card-value {
        font-size: 1.2em;
    }
}
";
    }

    private string GenerateMemoryBarChart(PhysicalMemoryDistribution before, PhysicalMemoryDistribution after)
    {
        var categories = new (string Label, long Before, long After)[]
        {
            ("空闲内存", (long)before.FreeMemory, (long)after.FreeMemory),
            ("进程私有", (long)before.ProcessPrivate, (long)before.ProcessPrivate),
            ("共享内存", (long)before.SharedMemory, (long)before.SharedMemory),
            ("系统缓存", (long)before.SystemCache, (long)before.SystemCache),
            ("分页池", (long)before.PagedPool, (long)before.PagedPool),
            ("非分页池", (long)before.NonPagedPool, (long)before.NonPagedPool),
        };

        long maxValue = categories.Max(c => Math.Max(c.Before, c.After));

        var sb = new StringBuilder();
        sb.AppendLine("<div class=\"bar-chart\">");

        sb.AppendLine("<div class=\"bar-row\">");
        sb.AppendLine("<div class=\"bar-label\"></div>");
        sb.AppendLine("<div class=\"bar-label\" style=\"text-align:center; color:#e74c3c;\">优化前</div>");
        sb.AppendLine("<div class=\"bar-label\"></div>");
        sb.AppendLine("<div class=\"bar-label\" style=\"text-align:center; color:#27ae60;\">优化后</div>");
        sb.AppendLine("</div>");

        foreach (var (label, beforeVal, afterVal) in categories)
        {
            double beforePercent = maxValue > 0 ? (double)beforeVal / maxValue * 100 : 0;
            double afterPercent = maxValue > 0 ? (double)afterVal / maxValue * 100 : 0;

            sb.AppendLine("<div class=\"bar-row\">");
            sb.AppendLine($"<div class=\"bar-label\">{label}</div>");
            sb.AppendLine("<div class=\"bar-track\">");
            sb.AppendLine($"<div class=\"bar-fill before\" style=\"width: {beforePercent:F1}%;\">{MemoryFormatter.FormatBytes(beforeVal)}</div>");
            sb.AppendLine("</div>");
            sb.AppendLine($"<div class=\"bar-value\">{MemoryFormatter.FormatBytes(beforeVal)}</div>");
            sb.AppendLine("<div class=\"bar-track\">");
            sb.AppendLine($"<div class=\"bar-fill after\" style=\"width: {afterPercent:F1}%;\">{MemoryFormatter.FormatBytes(afterVal)}</div>");
            sb.AppendLine("</div>");
            sb.AppendLine("</div>");
        }

        sb.AppendLine("</div>");
        return sb.ToString();
    }

    public async Task SaveReportAsync(string htmlContent, string filePath)
    {
        await File.WriteAllTextAsync(filePath, htmlContent, Encoding.UTF8);
    }
}
