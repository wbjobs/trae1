package com.task.scheduler.controller;

import com.task.scheduler.common.Result;
import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.service.TaskLogService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Page;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/logs")
public class TaskLogController {

    @Autowired
    private TaskLogService taskLogService;

    @GetMapping
    public Result<Page<TaskLog>> getTaskLogs(
            @RequestParam(required = false) Long taskId,
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "10") int size) {
        return Result.success(taskLogService.getTaskLogs(taskId, page, size));
    }

    @GetMapping("/{id}")
    public Result<TaskLog> getTaskLogById(@PathVariable Long id) {
        TaskLog taskLog = taskLogService.getTaskLogById(id);
        if (taskLog == null) {
            return Result.error("Log not found");
        }
        return Result.success(taskLog);
    }

    @DeleteMapping("/{id}")
    public Result<Void> deleteTaskLog(@PathVariable Long id) {
        taskLogService.deleteTaskLog(id);
        return Result.success();
    }

    @GetMapping("/all")
    public Result<Page<TaskLog>> getAllLogs(
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "10") int size) {
        return Result.success(taskLogService.getAllLogs(page, size));
    }
}
