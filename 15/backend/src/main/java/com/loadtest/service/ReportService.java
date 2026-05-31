package com.loadtest.service;

import com.loadtest.entity.TestResult;
import com.loadtest.entity.TestTask;
import com.loadtest.repository.TestResultRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.apache.poi.ss.usermodel.*;
import org.apache.poi.xssf.usermodel.XSSFWorkbook;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.*;
import java.nio.file.*;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class ReportService {

    private final TestResultRepository testResultRepository;

    @Value("${jmeter.report-dir:./reports}")
    private String reportDir;

    private static final int MAX_RESULTS_FOR_REPORT = 10000;
    private static final DateTimeFormatter FORMATTER = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    public String generateHtmlReport(TestTask task) throws IOException {
        List<TestResult> results = getResultsForReport(task.getId());
        String fileName = "report_" + task.getId() + "_" + System.currentTimeMillis() + ".html";
        Path filePath = Paths.get(reportDir, fileName);
        Files.createDirectories(filePath.getParent());

        String html = buildHtmlReport(task, results);
        Files.writeString(filePath, html);
        return filePath.toString();
    }

    public String generateExcelReport(TestTask task) throws IOException {
        List<TestResult> results = getResultsForReport(task.getId());
        String fileName = "report_" + task.getId() + "_" + System.currentTimeMillis() + ".xlsx";
        Path filePath = Paths.get(reportDir, fileName);
        Files.createDirectories(filePath.getParent());

        try (Workbook workbook = new XSSFWorkbook()) {
            CellStyle headerStyle = createHeaderStyle(workbook);
            CellStyle dataStyle = createDataStyle(workbook);
            CellStyle numberStyle = createNumberStyle(workbook);

            createSummarySheet(workbook, task, headerStyle, dataStyle, numberStyle);
            createDetailsSheet(workbook, results, headerStyle, dataStyle);

            try (FileOutputStream fos = new FileOutputStream(filePath.toFile())) {
                workbook.write(fos);
            }
        }
        return filePath.toString();
    }

    public String generatePdfReport(TestTask task) throws IOException {
        return generateHtmlReport(task);
    }

    private List<TestResult> getResultsForReport(Long taskId) {
        try {
            List<TestResult> results = testResultRepository.findByTaskIdOrderByTimestampAsc(taskId);
            if (results.size() > MAX_RESULTS_FOR_REPORT) {
                log.info("Truncating results from {} to {} for report", results.size(), MAX_RESULTS_FOR_REPORT);
                return results.subList(0, MAX_RESULTS_FOR_REPORT);
            }
            return results;
        } catch (Exception e) {
            log.error("Failed to get results for report", e);
            return new ArrayList<>();
        }
    }

    public Map<String, Object> getStatistics(Long taskId) {
        List<TestResult> results = testResultRepository.findByTaskIdOrderByTimestampAsc(taskId);
        return calculateStatistics(results);
    }

    public List<Map<String, Object>> getTimelineData(Long taskId) {
        List<TestResult> results = testResultRepository.findByTaskIdOrderByTimestampAsc(taskId);
        return results.stream()
                .limit(MAX_RESULTS_FOR_REPORT)
                .map(this::toMap)
                .collect(Collectors.toList());
    }

    public List<Map<String, Object>> getResponseTimeDistribution(Long taskId) {
        List<TestResult> results = testResultRepository.findByTaskId(taskId);
        if (results == null || results.isEmpty()) return new ArrayList<>();

        int[] ranges = {0, 100, 200, 500, 1000, 2000, 5000, 10000};
        String[] labels = {"0-100ms", "100-200ms", "200-500ms", "500ms-1s", "1-2s", "2-5s", "5-10s", "10s+"};
        int[] counts = new int[ranges.length];

        for (TestResult result : results) {
            if (result.getElapsed() == null) continue;
            long elapsed = result.getElapsed();
            for (int i = ranges.length - 1; i >= 0; i--) {
                if (elapsed >= ranges[i]) {
                    counts[i]++;
                    break;
                }
            }
        }

        List<Map<String, Object>> distribution = new ArrayList<>();
        for (int i = 0; i < labels.length; i++) {
            Map<String, Object> item = new LinkedHashMap<>();
            item.put("range", labels[i]);
            item.put("count", counts[i]);
            distribution.add(item);
        }
        return distribution;
    }

    private String buildHtmlReport(TestTask task, List<TestResult> results) {
        Map<String, Object> stats = calculateStatistics(results);

        StringBuilder html = new StringBuilder();
        html.append("<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n");
        html.append("<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n");
        html.append("<title>压测报告 - ").append(escapeHtml(task.getName())).append("</title>\n");
        html.append("<style>").append(getCssStyles()).append("</style>\n");
        html.append("</head>\n<body>\n");

        html.append("<div class=\"container\">\n");
        html.append("<header>\n<h1>接口压测报告</h1>\n");
        html.append("<p class=\"subtitle\">").append(escapeHtml(task.getName())).append("</p>\n");
        html.append("</header>\n");

        html.append("<section class=\"summary\">\n<h2>测试概要</h2>\n");
        html.append("<div class=\"summary-grid\">\n");
        html.append("<div class=\"summary-item\"><span class=\"label\">任务状态</span><span class=\"value\">")
                .append(task.getStatus() != null ? task.getStatus().name() : "UNKNOWN").append("</span></div>\n");
        if (task.getStartedAt() != null) {
            html.append("<div class=\"summary-item\"><span class=\"label\">开始时间</span><span class=\"value\">")
                    .append(task.getStartedAt().format(FORMATTER)).append("</span></div>\n");
        }
        if (task.getCompletedAt() != null) {
            html.append("<div class=\"summary-item\"><span class=\"label\">结束时间</span><span class=\"value\">")
                    .append(task.getCompletedAt().format(FORMATTER)).append("</span></div>\n");
        }
        if (task.getStartedAt() != null && task.getCompletedAt() != null) {
            long durationSeconds = java.time.Duration.between(task.getStartedAt(), task.getCompletedAt()).getSeconds();
            html.append("<div class=\"summary-item\"><span class=\"label\">测试时长</span><span class=\"value\">")
                    .append(durationSeconds).append("秒</span></div>\n");
        }
        html.append("</div>\n</section>\n");

        html.append("<section class=\"metrics\">\n<h2>性能指标</h2>\n");
        html.append("<div class=\"metrics-grid\">\n");
        html.append(buildMetricCard("总请求数", stats.get("totalRequests"), ""));
        html.append(buildMetricCard("成功数", stats.get("successCount"), "success"));
        html.append(buildMetricCard("失败数", stats.get("failureCount"), "error"));
        html.append(buildMetricCard("平均响应时间", formatNumber(stats.get("avgElapsed"), 2), "ms"));
        html.append(buildMetricCard("标准差", formatNumber(stats.get("stdDev"), 2), "ms"));
        html.append(buildMetricCard("最小响应时间", String.valueOf(stats.get("minElapsed")), "ms"));
        html.append(buildMetricCard("最大响应时间", String.valueOf(stats.get("maxElapsed")), "ms"));
        html.append(buildMetricCard("P50响应时间", formatNumber(stats.get("p50"), 2), "ms"));
        html.append(buildMetricCard("P90响应时间", formatNumber(stats.get("p90"), 2), "ms"));
        html.append(buildMetricCard("P95响应时间", formatNumber(stats.get("p95"), 2), "ms"));
        html.append(buildMetricCard("P99响应时间", formatNumber(stats.get("p99"), 2), "ms"));
        html.append(buildMetricCard("P99.9响应时间", formatNumber(stats.get("p999"), 2), "ms"));
        html.append(buildMetricCard("吞吐量", formatNumber(stats.get("throughput"), 2), "req/s"));
        html.append(buildMetricCard("错误率", formatNumber(stats.get("errorRate"), 2), "%"));
        html.append("</div>\n</section>\n");

        html.append("<section class=\"response-codes\">\n<h2>响应码分布</h2>\n");
        html.append("<table><thead><tr><th>响应码</th><th>数量</th><th>占比</th></tr></thead><tbody>\n");
        Map<String, Long> responseCodes = getResponseCodes(stats);
        long total = getTotalRequests(stats);
        responseCodes.forEach((code, count) -> {
            double percentage = total > 0 ? (count * 100.0 / total) : 0;
            html.append("<tr><td>").append(escapeHtml(code)).append("</td><td>").append(count)
                    .append("</td><td>").append(String.format("%.2f%%", percentage)).append("</td></tr>\n");
        });
        html.append("</tbody></table>\n</section>\n");

        html.append("<section class=\"details\">\n<h2>详细数据</h2>\n");
        html.append("<p class=\"data-info\">共 ").append(results.size()).append(" 条记录");
        if (results.size() >= MAX_RESULTS_FOR_REPORT) {
            html.append("（已截断至 ").append(MAX_RESULTS_FOR_REPORT).append(" 条）");
        }
        html.append("</p>\n");
        html.append("<div class=\"table-container\">\n");
        html.append("<table><thead><tr><th>时间</th><th>响应时间(ms)</th><th>状态码</th><th>是否成功</th><th>字节数</th></tr></thead><tbody>\n");
        for (TestResult result : results) {
            html.append("<tr>");
            html.append("<td>").append(result.getTimestamp() != null ? result.getTimestamp().format(FORMATTER) : "").append("</td>");
            html.append("<td>").append(result.getElapsed() != null ? result.getElapsed() : "").append("</td>");
            html.append("<td>").append(escapeHtml(result.getResponseCode() != null ? result.getResponseCode() : "")).append("</td>");
            html.append("<td class=\"").append(Boolean.TRUE.equals(result.getSuccess()) ? "success" : "error")
                    .append("\">").append(Boolean.TRUE.equals(result.getSuccess()) ? "成功" : "失败").append("</td>");
            html.append("<td>").append(result.getBytes() != null ? result.getBytes() : "").append("</td>");
            html.append("</tr>\n");
        }
        html.append("</tbody></table>\n</div>\n</section>\n");

        html.append("<footer>\n<p>生成时间: ").append(java.time.LocalDateTime.now().format(FORMATTER)).append("</p>\n");
        html.append("<p>接口压测配置管理平台</p>\n");
        html.append("</footer>\n</div>\n</body>\n</html>");

        return html.toString();
    }

    private String buildMetricCard(String label, Object value, String cssClass) {
        StringBuilder sb = new StringBuilder();
        sb.append("<div class=\"metric-card");
        if (!cssClass.isEmpty() && (cssClass.equals("success") || cssClass.equals("error"))) {
            sb.append(" ").append(cssClass);
        }
        sb.append("\"><div class=\"metric-label\">").append(label).append("</div>");
        sb.append("<div class=\"metric-value\">").append(value);
        if (!cssClass.isEmpty() && !cssClass.equals("success") && !cssClass.equals("error")) {
            sb.append(" ").append(cssClass);
        }
        sb.append("</div></div>\n");
        return sb.toString();
    }

    private String escapeHtml(String value) {
        if (value == null) return "";
        return value.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace("\"", "&quot;");
    }

    private String formatNumber(Object value, int decimals) {
        if (value == null) return "0";
        try {
            double num = Double.parseDouble(value.toString());
            return String.format("%." + decimals + "f", num);
        } catch (NumberFormatException e) {
            return value.toString();
        }
    }

    private Map<String, Long> getResponseCodes(Map<String, Object> stats) {
        Object obj = stats.get("responseCodes");
        if (obj instanceof Map) {
            @SuppressWarnings("unchecked")
            Map<String, Long> map = (Map<String, Long>) obj;
            return map;
        }
        return new HashMap<>();
    }

    private long getTotalRequests(Map<String, Object> stats) {
        Object obj = stats.get("totalRequests");
        if (obj instanceof Number) {
            return ((Number) obj).longValue();
        }
        return 0;
    }

    private String getCssStyles() {
        return """
            * { margin: 0; padding: 0; box-sizing: border-box; }
            body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif; background: #f5f7fa; color: #333; line-height: 1.6; }
            .container { max-width: 1200px; margin: 0 auto; padding: 20px; }
            header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 40px; border-radius: 12px; margin-bottom: 30px; }
            header h1 { font-size: 28px; margin-bottom: 10px; }
            .subtitle { opacity: 0.9; font-size: 16px; }
            section { background: white; border-radius: 12px; padding: 30px; margin-bottom: 20px; box-shadow: 0 2px 12px rgba(0,0,0,0.08); }
            section h2 { font-size: 20px; color: #333; margin-bottom: 20px; padding-bottom: 10px; border-bottom: 2px solid #f0f0f0; }
            .summary-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; }
            .summary-item { display: flex; flex-direction: column; padding: 15px; background: #f8f9fa; border-radius: 8px; }
            .summary-item .label { font-size: 14px; color: #666; margin-bottom: 5px; }
            .summary-item .value { font-size: 18px; font-weight: 600; color: #333; }
            .metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 15px; }
            .metric-card { padding: 20px; border-radius: 10px; text-align: center; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; }
            .metric-card.success { background: linear-gradient(135deg, #11998e 0%, #38ef7d 100%); }
            .metric-card.error { background: linear-gradient(135deg, #eb3349 0%, #f45c43 100%); }
            .metric-label { font-size: 14px; opacity: 0.9; margin-bottom: 8px; }
            .metric-value { font-size: 24px; font-weight: 700; }
            table { width: 100%; border-collapse: collapse; }
            th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid #eee; }
            th { background: #f8f9fa; font-weight: 600; color: #555; }
            tr:hover { background: #f8f9fa; }
            td.success { color: #10b981; font-weight: 600; }
            td.error { color: #ef4444; font-weight: 600; }
            .table-container { max-height: 500px; overflow-y: auto; }
            .data-info { color: #666; font-size: 14px; margin-bottom: 10px; }
            footer { text-align: center; padding: 20px; color: #888; font-size: 14px; }
            """;
    }

    private void createSummarySheet(Workbook workbook, TestTask task, CellStyle headerStyle, CellStyle dataStyle, CellStyle numberStyle) {
        Sheet sheet = workbook.createSheet("测试概要");
        int rowNum = 0;

        Object[][] summaryData = {
                {"任务名称", task.getName(), false},
                {"任务状态", task.getStatus() != null ? task.getStatus().name() : "UNKNOWN", false},
                {"总请求数", safeLong(task.getTotalRequests()), true},
                {"成功数", safeLong(task.getSuccessCount()), true},
                {"失败数", safeLong(task.getFailureCount()), true},
                {"平均响应时间(ms)", safeDouble(task.getAvgResponseTime()), true},
                {"P95响应时间(ms)", safeDouble(task.getP95ResponseTime()), true},
                {"P99响应时间(ms)", safeDouble(task.getP99ResponseTime()), true},
                {"吞吐量(req/s)", safeDouble(task.getThroughput()), true},
                {"错误率(%)", safeDouble(task.getErrorRate()), true},
        };

        for (Object[] data : summaryData) {
            Row row = sheet.createRow(rowNum++);
            Cell cell0 = row.createCell(0);
            cell0.setCellValue((String) data[0]);
            cell0.setCellStyle(headerStyle);
            Cell cell1 = row.createCell(1);
            cell1.setCellStyle((Boolean) data[2] ? numberStyle : dataStyle);
            if (data[1] instanceof Number) {
                cell1.setCellValue(((Number) data[1]).doubleValue());
            } else {
                cell1.setCellValue(String.valueOf(data[1]));
            }
        }

        sheet.setColumnWidth(0, 5000);
        sheet.setColumnWidth(1, 8000);
    }

    private long safeLong(Long value) {
        return value != null ? value : 0L;
    }

    private double safeDouble(Double value) {
        return value != null ? value : 0.0;
    }

    private void createDetailsSheet(Workbook workbook, List<TestResult> results, CellStyle headerStyle, CellStyle dataStyle) {
        Sheet sheet = workbook.createSheet("详细数据");
        int rowNum = 0;

        Row headerRow = sheet.createRow(rowNum++);
        String[] headers = {"时间", "响应时间(ms)", "状态码", "是否成功", "字节数", "发送字节数", "活跃线程", "总线程"};
        for (int i = 0; i < headers.length; i++) {
            Cell cell = headerRow.createCell(i);
            cell.setCellValue(headers[i]);
            cell.setCellStyle(headerStyle);
        }

        int maxRows = Math.min(results.size(), MAX_RESULTS_FOR_REPORT);
        for (int i = 0; i < maxRows; i++) {
            TestResult result = results.get(i);
            Row row = sheet.createRow(rowNum++);
            int colNum = 0;
            createCell(row, colNum++, result.getTimestamp() != null ? result.getTimestamp().format(FORMATTER) : "", dataStyle);
            createCell(row, colNum++, String.valueOf(result.getElapsed() != null ? result.getElapsed() : ""), dataStyle);
            createCell(row, colNum++, result.getResponseCode() != null ? result.getResponseCode() : "", dataStyle);
            createCell(row, colNum++, Boolean.TRUE.equals(result.getSuccess()) ? "成功" : "失败", dataStyle);
            createCell(row, colNum++, String.valueOf(result.getBytes() != null ? result.getBytes() : ""), dataStyle);
            createCell(row, colNum++, String.valueOf(result.getSentBytes() != null ? result.getSentBytes() : ""), dataStyle);
            createCell(row, colNum++, String.valueOf(result.getGrpThreads() != null ? result.getGrpThreads() : ""), dataStyle);
            createCell(row, colNum++, String.valueOf(result.getAllThreads() != null ? result.getAllThreads() : ""), dataStyle);
        }

        for (int i = 0; i < headers.length; i++) {
            sheet.setColumnWidth(i, 5000);
        }
    }

    private void createCell(Row row, int column, String value, CellStyle style) {
        Cell cell = row.createCell(column);
        cell.setCellValue(value != null ? value : "");
        cell.setCellStyle(style);
    }

    private CellStyle createHeaderStyle(Workbook workbook) {
        CellStyle style = workbook.createCellStyle();
        Font font = workbook.createFont();
        font.setBold(true);
        style.setFont(font);
        style.setFillForegroundColor(IndexedColors.GREY_25_PERCENT.getIndex());
        style.setFillPattern(FillPatternType.SOLID_FOREGROUND);
        style.setBorderBottom(BorderStyle.THIN);
        style.setBorderTop(BorderStyle.THIN);
        style.setBorderLeft(BorderStyle.THIN);
        style.setBorderRight(BorderStyle.THIN);
        return style;
    }

    private CellStyle createDataStyle(Workbook workbook) {
        CellStyle style = workbook.createCellStyle();
        style.setBorderBottom(BorderStyle.THIN);
        style.setBorderTop(BorderStyle.THIN);
        style.setBorderLeft(BorderStyle.THIN);
        style.setBorderRight(BorderStyle.THIN);
        return style;
    }

    private CellStyle createNumberStyle(Workbook workbook) {
        CellStyle style = createDataStyle(workbook);
        DataFormat format = workbook.createDataFormat();
        style.setDataFormat(format.getFormat("#,##0.00"));
        return style;
    }

    private Map<String, Object> calculateStatistics(List<TestResult> results) {
        Map<String, Object> stats = new HashMap<>();
        if (results == null || results.isEmpty()) {
            stats.put("totalRequests", 0L);
            stats.put("successCount", 0L);
            stats.put("failureCount", 0L);
            stats.put("avgElapsed", 0.0);
            stats.put("minElapsed", 0L);
            stats.put("maxElapsed", 0L);
            stats.put("p50", 0.0);
            stats.put("p90", 0.0);
            stats.put("p95", 0.0);
            stats.put("p99", 0.0);
            stats.put("p999", 0.0);
            stats.put("stdDev", 0.0);
            stats.put("throughput", 0.0);
            stats.put("errorRate", 0.0);
            stats.put("responseCodes", new HashMap<String, Long>());
            return stats;
        }

        long totalRequests = results.size();
        long successCount = results.stream().filter(r -> Boolean.TRUE.equals(r.getSuccess())).count();
        long failureCount = totalRequests - successCount;

        List<Long> elapsedList = results.stream()
                .map(TestResult::getElapsed)
                .filter(Objects::nonNull)
                .sorted()
                .collect(Collectors.toList());

        double avgElapsed = elapsedList.stream().mapToLong(Long::longValue).average().orElse(0);
        long minElapsed = elapsedList.isEmpty() ? 0 : elapsedList.get(0);
        long maxElapsed = elapsedList.isEmpty() ? 0 : elapsedList.get(elapsedList.size() - 1);
        
        double stdDev = calculateStandardDeviation(elapsedList, avgElapsed);

        double p50 = calculatePercentileValue(elapsedList, 50);
        double p90 = calculatePercentileValue(elapsedList, 90);
        double p95 = calculatePercentileValue(elapsedList, 95);
        double p99 = calculatePercentileValue(elapsedList, 99);
        double p999 = calculatePercentileValue(elapsedList, 99.9);

        Map<String, Long> responseCodes = results.stream()
                .filter(r -> r.getResponseCode() != null)
                .collect(Collectors.groupingBy(TestResult::getResponseCode, Collectors.counting()));

        long durationSeconds = 60;
        if (results.size() > 1) {
            java.time.LocalDateTime firstTime = results.get(0).getTimestamp();
            java.time.LocalDateTime lastTime = results.get(results.size() - 1).getTimestamp();
            if (firstTime != null && lastTime != null) {
                durationSeconds = Math.max(1, java.time.Duration.between(firstTime, lastTime).getSeconds());
            }
        }

        stats.put("totalRequests", totalRequests);
        stats.put("successCount", successCount);
        stats.put("failureCount", failureCount);
        stats.put("avgElapsed", avgElapsed);
        stats.put("minElapsed", minElapsed);
        stats.put("maxElapsed", maxElapsed);
        stats.put("p50", p50);
        stats.put("p90", p90);
        stats.put("p95", p95);
        stats.put("p99", p99);
        stats.put("p999", p999);
        stats.put("stdDev", stdDev);
        stats.put("throughput", totalRequests / (double) durationSeconds);
        stats.put("errorRate", totalRequests > 0 ? (failureCount * 100.0 / totalRequests) : 0);
        stats.put("responseCodes", responseCodes);

        return stats;
    }

    private double calculateStandardDeviation(List<Long> values, double mean) {
        if (values == null || values.isEmpty()) return 0.0;
        double sumSquaredDiff = values.stream()
                .mapToDouble(v -> Math.pow(v - mean, 2))
                .sum();
        return Math.sqrt(sumSquaredDiff / values.size());
    }

    private double calculatePercentileValue(List<Long> sortedValues, double percentile) {
        if (sortedValues == null || sortedValues.isEmpty()) return 0;
        double rank = percentile / 100.0 * (sortedValues.size() - 1);
        int lowerIndex = (int) Math.floor(rank);
        int upperIndex = (int) Math.ceil(rank);
        if (lowerIndex == upperIndex) {
            return sortedValues.get(lowerIndex);
        }
        double fraction = rank - lowerIndex;
        return sortedValues.get(lowerIndex) + fraction * (sortedValues.get(upperIndex) - sortedValues.get(lowerIndex));
    }

    private Map<String, Object> toMap(TestResult result) {
        Map<String, Object> map = new LinkedHashMap<>();
        map.put("timestamp", result.getTimestamp() != null ? result.getTimestamp().toString() : null);
        map.put("elapsed", result.getElapsed());
        map.put("responseCode", result.getResponseCode());
        map.put("success", result.getSuccess());
        map.put("bytes", result.getBytes());
        map.put("latency", result.getLatency());
        map.put("allThreads", result.getAllThreads());
        return map;
    }
}
