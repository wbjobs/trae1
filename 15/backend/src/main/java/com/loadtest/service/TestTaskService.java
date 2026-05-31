package com.loadtest.service;

import com.loadtest.dto.TestTaskDTO;
import com.loadtest.entity.TestConfig;
import com.loadtest.entity.TestTask;
import com.loadtest.repository.TestTaskRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class TestTaskService {

    private final TestTaskRepository testTaskRepository;
    private final TestConfigService testConfigService;
    private final JmeterService jmeterService;
    private final ReportService reportService;

    @Value("${jmeter.report-dir:./reports}")
    private String reportDir;

    public List<TestTaskDTO> getAllTasks() {
        return testTaskRepository.findAllByOrderByCreatedAtDesc()
                .stream()
                .map(this::toDTO)
                .collect(Collectors.toList());
    }

    public TestTaskDTO getTaskById(Long id) {
        return testTaskRepository.findById(id)
                .map(this::toDTO)
                .orElseThrow(() -> new RuntimeException("Task not found: " + id));
    }

    public TestTask getTaskEntityById(Long id) {
        return testTaskRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("Task not found: " + id));
    }

    @Transactional
    public TestTaskDTO createTask(Long configId, String taskName, String priority) {
        TestConfig config = testConfigService.getConfigEntityById(configId);

        TestTask.TaskPriority taskPriority = TestTask.TaskPriority.MEDIUM;
        if (priority != null && !priority.isEmpty()) {
            try {
                taskPriority = TestTask.TaskPriority.valueOf(priority.toUpperCase());
            } catch (IllegalArgumentException e) {
                log.warn("Invalid priority: {}, using MEDIUM as default", priority);
            }
        }

        TestTask task = TestTask.builder()
                .name(taskName != null ? taskName : config.getName() + " - Test")
                .configId(configId)
                .status(TestTask.TaskStatus.PENDING)
                .priority(taskPriority)
                .build();

        TestTask saved = testTaskRepository.save(task);
        log.info("Created test task: {} with priority: {}", saved.getName(), taskPriority);
        return toDTO(saved);
    }

    @Transactional
    public TestTaskDTO startTask(Long taskId) {
        TestTask task = getTaskEntityById(taskId);
        TestConfig config = testConfigService.getConfigEntityById(task.getConfigId());

        if (task.getStatus() == TestTask.TaskStatus.RUNNING) {
            throw new RuntimeException("Task is already running");
        }

        jmeterService.executeTask(task, config);
        return toDTO(task);
    }

    @Transactional
    public TestTaskDTO stopTask(Long taskId) {
        TestTask task = getTaskEntityById(taskId);

        if (task.getStatus() != TestTask.TaskStatus.RUNNING) {
            throw new RuntimeException("Task is not running");
        }

        jmeterService.stopTask(taskId);
        return toDTO(task);
    }

    @Transactional
    public void deleteTask(Long taskId) {
        TestTask task = getTaskEntityById(taskId);

        if (task.getStatus() == TestTask.TaskStatus.RUNNING) {
            jmeterService.stopTask(taskId);
        }

        testTaskRepository.deleteById(taskId);
        log.info("Deleted test task: {}", taskId);
    }

    public String generateReport(Long taskId, String format) {
        TestTask task = getTaskEntityById(taskId);

        if (task.getStatus() == TestTask.TaskStatus.RUNNING || task.getStatus() == TestTask.TaskStatus.PENDING) {
            throw new RuntimeException("Can only generate report for completed, failed or stopped tasks");
        }

        try {
            Path reportPath = Paths.get(reportDir);
            Files.createDirectories(reportPath);

            String reportFilePath;
            switch (format.toLowerCase()) {
                case "pdf":
                    reportFilePath = reportService.generatePdfReport(task);
                    break;
                case "excel":
                case "xlsx":
                    reportFilePath = reportService.generateExcelReport(task);
                    break;
                case "html":
                default:
                    reportFilePath = reportService.generateHtmlReport(task);
                    break;
            }

            task.setReportPath(reportFilePath);
            testTaskRepository.save(task);

            return reportFilePath;
        } catch (Exception e) {
            log.error("Failed to generate report", e);
            throw new RuntimeException("Report generation failed: " + e.getMessage(), e);
        }
    }

    public boolean isTaskRunning(Long taskId) {
        return jmeterService.isRunning(taskId);
    }

    public Map<String, Object> compareTasks(List<Long> taskIds) {
        List<TestTask> tasks = testTaskRepository.findAllById(taskIds);
        
        Map<String, Object> result = new java.util.LinkedHashMap<>();
        result.put("taskIds", taskIds);
        result.put("comparisonCount", tasks.size());
        
        List<Map<String, Object>> taskMetrics = new java.util.ArrayList<>();
        for (TestTask task : tasks) {
            Map<String, Object> metrics = new java.util.LinkedHashMap<>();
            metrics.put("id", task.getId());
            metrics.put("name", task.getName());
            metrics.put("status", task.getStatus() != null ? task.getStatus().name() : "");
            metrics.put("priority", task.getPriority() != null ? task.getPriority().name() : "MEDIUM");
            metrics.put("totalRequests", task.getTotalRequests() != null ? task.getTotalRequests() : 0);
            metrics.put("successCount", task.getSuccessCount() != null ? task.getSuccessCount() : 0);
            metrics.put("failureCount", task.getFailureCount() != null ? task.getFailureCount() : 0);
            metrics.put("avgResponseTime", task.getAvgResponseTime() != null ? task.getAvgResponseTime() : 0);
            metrics.put("minResponseTime", task.getMinResponseTime() != null ? task.getMinResponseTime() : 0);
            metrics.put("maxResponseTime", task.getMaxResponseTime() != null ? task.getMaxResponseTime() : 0);
            metrics.put("p95ResponseTime", task.getP95ResponseTime() != null ? task.getP95ResponseTime() : 0);
            metrics.put("p99ResponseTime", task.getP99ResponseTime() != null ? task.getP99ResponseTime() : 0);
            metrics.put("throughput", task.getThroughput() != null ? task.getThroughput() : 0);
            metrics.put("errorRate", task.getErrorRate() != null ? task.getErrorRate() : 0);
            metrics.put("startedAt", task.getStartedAt() != null ? task.getStartedAt().toString() : null);
            metrics.put("completedAt", task.getCompletedAt() != null ? task.getCompletedAt().toString() : null);
            taskMetrics.add(metrics);
        }
        result.put("tasks", taskMetrics);
        
        if (tasks.size() >= 2) {
            Map<String, Object> summary = new java.util.LinkedHashMap<>();
            TestTask first = tasks.get(0);
            TestTask last = tasks.get(tasks.size() - 1);
            
            summary.put("avgResponseTimeChange", calculateChange(
                first.getAvgResponseTime(), last.getAvgResponseTime()));
            summary.put("throughputChange", calculateChange(
                first.getThroughput(), last.getThroughput()));
            summary.put("errorRateChange", calculateChange(
                first.getErrorRate(), last.getErrorRate()));
            summary.put("p95ResponseTimeChange", calculateChange(
                first.getP95ResponseTime(), last.getP95ResponseTime()));
            
            result.put("summary", summary);
        }
        
        return result;
    }
    
    private Double calculateChange(Double oldValue, Double newValue) {
        if (oldValue == null || newValue == null || oldValue == 0) return null;
        return ((newValue - oldValue) / oldValue) * 100;
    }

    public List<TestTaskDTO> getTaskHistoryByConfig(Long configId) {
        return testTaskRepository.findByConfigIdOrderByCreatedAtDesc(configId)
                .stream()
                .limit(20)
                .map(this::toDTO)
                .collect(Collectors.toList());
    }

    private TestTaskDTO toDTO(TestTask task) {
        return TestTaskDTO.builder()
                .id(task.getId())
                .name(task.getName())
                .configId(task.getConfigId())
                .status(task.getStatus().name())
                .startedAt(task.getStartedAt() != null ? task.getStartedAt().toString() : null)
                .completedAt(task.getCompletedAt() != null ? task.getCompletedAt().toString() : null)
                .createdAt(task.getCreatedAt() != null ? task.getCreatedAt().toString() : null)
                .updatedAt(task.getUpdatedAt() != null ? task.getUpdatedAt().toString() : null)
                .totalRequests(task.getTotalRequests())
                .successCount(task.getSuccessCount())
                .failureCount(task.getFailureCount())
                .avgResponseTime(task.getAvgResponseTime())
                .minResponseTime(task.getMinResponseTime())
                .maxResponseTime(task.getMaxResponseTime())
                .p95ResponseTime(task.getP95ResponseTime())
                .p99ResponseTime(task.getP99ResponseTime())
                .throughput(task.getThroughput())
                .errorRate(task.getErrorRate())
                .resultSummary(task.getResultSummary())
                .priority(task.getPriority() != null ? task.getPriority().name() : "MEDIUM")
                .build();
    }
}
