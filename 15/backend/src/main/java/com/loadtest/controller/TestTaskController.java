package com.loadtest.controller;

import com.loadtest.dto.ApiResponse;
import com.loadtest.dto.TestTaskDTO;
import com.loadtest.service.ReportService;
import com.loadtest.service.TestTaskService;
import lombok.RequiredArgsConstructor;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.io.File;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/tasks")
@RequiredArgsConstructor
public class TestTaskController {

    private final TestTaskService testTaskService;
    private final ReportService reportService;

    @GetMapping
    public ApiResponse<List<TestTaskDTO>> getAllTasks() {
        return ApiResponse.success(testTaskService.getAllTasks());
    }

    @GetMapping("/{id}")
    public ApiResponse<TestTaskDTO> getTaskById(@PathVariable Long id) {
        return ApiResponse.success(testTaskService.getTaskById(id));
    }

    @PostMapping
    public ApiResponse<TestTaskDTO> createTask(@RequestBody Map<String, Object> request) {
        Long configId = Long.valueOf(request.get("configId").toString());
        String taskName = request.get("name") != null ? request.get("name").toString() : null;
        String priority = request.get("priority") != null ? request.get("priority").toString() : null;
        return ApiResponse.success("任务创建成功", testTaskService.createTask(configId, taskName, priority));
    }

    @PostMapping("/{id}/start")
    public ApiResponse<TestTaskDTO> startTask(@PathVariable Long id) {
        return ApiResponse.success("任务启动成功", testTaskService.startTask(id));
    }

    @PostMapping("/{id}/stop")
    public ApiResponse<TestTaskDTO> stopTask(@PathVariable Long id) {
        return ApiResponse.success("任务停止成功", testTaskService.stopTask(id));
    }

    @DeleteMapping("/{id}")
    public ApiResponse<Void> deleteTask(@PathVariable Long id) {
        testTaskService.deleteTask(id);
        return ApiResponse.success("任务删除成功", null);
    }

    @GetMapping("/{id}/statistics")
    public ApiResponse<Map<String, Object>> getStatistics(@PathVariable Long id) {
        return ApiResponse.success(reportService.getStatistics(id));
    }

    @GetMapping("/{id}/timeline")
    public ApiResponse<List<Map<String, Object>>> getTimelineData(@PathVariable Long id) {
        return ApiResponse.success(reportService.getTimelineData(id));
    }

    @GetMapping("/{id}/distribution")
    public ApiResponse<List<Map<String, Object>>> getResponseTimeDistribution(@PathVariable Long id) {
        return ApiResponse.success(reportService.getResponseTimeDistribution(id));
    }

    @PostMapping("/{id}/report")
    public ApiResponse<Map<String, String>> generateReport(@PathVariable Long id, @RequestParam(defaultValue = "html") String format) {
        String reportPath = testTaskService.generateReport(id, format);
        return ApiResponse.success("报告生成成功", Map.of("reportPath", reportPath, "format", format));
    }

    @GetMapping("/{id}/report/download")
    public ResponseEntity<Resource> downloadReport(@PathVariable Long id, @RequestParam(defaultValue = "html") String format) {
        try {
            String reportPath = testTaskService.generateReport(id, format);
            File file = new File(reportPath);

            if (!file.exists()) {
                return ResponseEntity.notFound().build();
            }

            Resource resource = new FileSystemResource(file);
            String contentType = "application/octet-stream";
            String extension = "html";

            switch (format.toLowerCase()) {
                case "pdf":
                    contentType = "application/pdf";
                    extension = "html";
                    break;
                case "excel":
                case "xlsx":
                    contentType = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
                    extension = "xlsx";
                    break;
                default:
                    contentType = "text/html";
                    extension = "html";
            }

            return ResponseEntity.ok()
                    .contentType(MediaType.parseMediaType(contentType))
                    .header(HttpHeaders.CONTENT_DISPOSITION, "attachment; filename=\"report_" + id + "." + extension + "\"")
                    .body(resource);
        } catch (Exception e) {
            return ResponseEntity.internalServerError().build();
        }
    }

    @GetMapping("/compare")
    public ApiResponse<Map<String, Object>> compareTasks(@RequestParam List<Long> taskIds) {
        return ApiResponse.success(testTaskService.compareTasks(taskIds));
    }

    @GetMapping("/config/{configId}/history")
    public ApiResponse<List<TestTaskDTO>> getTaskHistoryByConfig(@PathVariable Long configId) {
        return ApiResponse.success(testTaskService.getTaskHistoryByConfig(configId));
    }
}
