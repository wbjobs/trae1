package com.sharding.sync.controller;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.common.Result;
import com.sharding.sync.sync.dto.SyncTaskDTO;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.DataSyncService;
import com.sharding.sync.sync.service.SyncLogService;
import com.sharding.sync.sync.service.SyncRetryService;
import com.sharding.sync.sync.service.SyncTaskService;
import lombok.RequiredArgsConstructor;
import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;

import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/sync")
@RequiredArgsConstructor
public class SyncController {

    private final DataSyncService dataSyncService;
    private final SyncTaskService syncTaskService;
    private final SyncLogService syncLogService;
    private final SyncRetryService syncRetryService;

    @PostMapping("/full")
    public Result<SyncTask> triggerFullSync(@Validated @RequestBody SyncTaskDTO dto) {
        return Result.success(dataSyncService.submitFullSync(
                dto.getLogicTable(), dto.getSourceDs(), dto.getTargetDs(), dto.getParams()));
    }

    @PostMapping("/incremental")
    public Result<SyncTask> triggerIncrementalSync(@Validated @RequestBody SyncTaskDTO dto) {
        return Result.success(dataSyncService.submitIncrementalSync(
                dto.getLogicTable(), dto.getSourceDs(), dto.getTargetDs(), dto.getParams()));
    }

    @PostMapping("/binlog")
    public Result<SyncTask> triggerBinlogSync(@Validated @RequestBody SyncTaskDTO dto) {
        return Result.success(dataSyncService.submitBinlogSync(
                dto.getLogicTable(), dto.getSourceDs(), dto.getTargetDs(), dto.getParams()));
    }

    @GetMapping("/status/{taskNo}")
    public Result<Map<String, Object>> status(@PathVariable String taskNo) {
        return Result.success(syncTaskService.getStatus(taskNo));
    }

    @GetMapping("/task/{id}")
    public Result<SyncTask> getTask(@PathVariable Long id) {
        return Result.success(syncTaskService.getById(id));
    }

    @GetMapping("/task-by-no/{taskNo}")
    public Result<SyncTask> getTaskByNo(@PathVariable String taskNo) {
        return Result.success(syncTaskService.getByTaskNo(taskNo));
    }

    @GetMapping("/page")
    public Result<IPage<SyncTask>> page(@RequestParam(defaultValue = "1") Integer current,
                                        @RequestParam(defaultValue = "10") Integer size,
                                        @RequestParam(required = false) String logicTable,
                                        @RequestParam(required = false) String syncType,
                                        @RequestParam(required = false) String status) {
        return Result.success(syncTaskService.page(new Page<>(current, size), logicTable, syncType, status));
    }

    @GetMapping("/recent")
    public Result<List<SyncTask>> recent(@RequestParam(required = false) String logicTable,
                                         @RequestParam(defaultValue = "20") Integer limit) {
        return Result.success(syncTaskService.listRecent(logicTable, limit));
    }

    @PostMapping("/cancel/{taskNo}")
    public Result<Boolean> cancel(@PathVariable String taskNo) {
        return Result.success(syncTaskService.cancel(taskNo));
    }

    @GetMapping("/logs")
    public Result<List<Map<String, Object>>> getLogs(
            @RequestParam(required = false) String logicTable,
            @RequestParam(required = false) String syncType,
            @RequestParam(required = false) String status,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime startTime,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime endTime,
            @RequestParam(defaultValue = "1000") Integer limit) {
        return Result.success(syncLogService.exportLogs(logicTable, syncType, status, startTime, endTime, limit));
    }

    @GetMapping("/logs/csv")
    public ResponseEntity<byte[]> exportCsv(
            @RequestParam(required = false) String logicTable,
            @RequestParam(required = false) String syncType,
            @RequestParam(required = false) String status,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime startTime,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime endTime,
            @RequestParam(defaultValue = "1000") Integer limit) {
        String csv = syncLogService.exportCsv(logicTable, syncType, status, startTime, endTime, limit);
        String filename = "sync_logs_" + System.currentTimeMillis() + ".csv";
        HttpHeaders headers = new HttpHeaders();
        headers.setContentType(MediaType.parseMediaType("text/csv; charset=UTF-8"));
        headers.setContentDispositionFormData("attachment", filename);
        return ResponseEntity.ok()
                .headers(headers)
                .body(csv.getBytes(StandardCharsets.UTF_8));
    }

    @GetMapping("/statistics")
    public Result<Map<String, Object>> statistics(
            @RequestParam(required = false) String logicTable,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime startTime,
            @RequestParam(required = false) @DateTimeFormat(pattern = "yyyy-MM-dd HH:mm:ss") LocalDateTime endTime) {
        return Result.success(syncLogService.getSyncStatistics(logicTable, startTime, endTime));
    }

    @PostMapping("/retry/{taskNo}")
    public Result<SyncTask> retryTask(@PathVariable String taskNo) {
        SyncTask original = syncTaskService.getByTaskNo(taskNo);
        if (original == null) {
            return Result.fail("任务不存在: " + taskNo);
        }
        SyncTask retryTask = syncRetryService.retryTask(original);
        if (retryTask == null) {
            return Result.fail("无法重试, 请检查任务状态或重试次数是否已达上限");
        }
        return Result.success(retryTask);
    }
}
