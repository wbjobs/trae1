package com.task.scheduler.controller;

import com.task.scheduler.common.Result;
import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.service.TaskConfigService;
import com.task.scheduler.service.TaskSchedulerService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/tasks")
public class TaskConfigController {

    @Autowired
    private TaskConfigService taskConfigService;

    @Autowired
    private TaskSchedulerService taskSchedulerService;

    @GetMapping
    public Result<List<TaskConfig>> getAllTasks() {
        return Result.success(taskConfigService.getAllTasks());
    }

    @GetMapping("/{id}")
    public Result<TaskConfig> getTaskById(@PathVariable Long id) {
        TaskConfig taskConfig = taskConfigService.getTaskById(id);
        if (taskConfig == null) {
            return Result.error("Task not found");
        }
        return Result.success(taskConfig);
    }

    @PostMapping
    public Result<TaskConfig> createTask(@RequestBody TaskConfig taskConfig) {
        return Result.success(taskConfigService.createTask(taskConfig));
    }

    @PutMapping("/{id}")
    public Result<TaskConfig> updateTask(@PathVariable Long id, @RequestBody TaskConfig taskConfig) {
        TaskConfig updated = taskConfigService.updateTask(id, taskConfig);
        if (updated == null) {
            return Result.error("Task not found");
        }
        return Result.success(updated);
    }

    @DeleteMapping("/{id}")
    public Result<Void> deleteTask(@PathVariable Long id) {
        taskConfigService.deleteTask(id);
        return Result.success();
    }

    @PostMapping("/{id}/start")
    public Result<Void> startTask(@PathVariable Long id) {
        try {
            taskSchedulerService.startTask(id);
            return Result.success();
        } catch (Exception e) {
            return Result.error(e.getMessage());
        }
    }

    @PostMapping("/{id}/stop")
    public Result<Void> stopTask(@PathVariable Long id) {
        try {
            taskSchedulerService.stopTask(id);
            return Result.success();
        } catch (Exception e) {
            return Result.error(e.getMessage());
        }
    }

    @PostMapping("/{id}/execute")
    public Result<Void> executeTask(@PathVariable Long id) {
        try {
            taskSchedulerService.executeTaskOnce(id);
            return Result.success();
        } catch (Exception e) {
            return Result.error(e.getMessage());
        }
    }

    @PostMapping("/{id}/distribute")
    public Result<Void> distributeTask(@PathVariable Long id, @RequestBody Map<String, List<String>> params) {
        try {
            List<String> serverNames = params.get("serverNames");
            if (serverNames == null || serverNames.isEmpty()) {
                return Result.error("Server names cannot be empty");
            }
            taskSchedulerService.distributeTaskToServers(id, serverNames);
            return Result.success();
        } catch (Exception e) {
            return Result.error(e.getMessage());
        }
    }

    @PostMapping("/batch-execute")
    public Result<Void> batchExecuteTasks(@RequestBody List<Long> taskIds) {
        try {
            taskSchedulerService.batchExecuteTasks(taskIds);
            return Result.success();
        } catch (Exception e) {
            return Result.error(e.getMessage());
        }
    }

    @GetMapping("/server/{serverName}")
    public Result<List<TaskConfig>> getTasksByServer(@PathVariable String serverName) {
        return Result.success(taskConfigService.getTasksByServer(serverName));
    }

    @PostMapping("/validate-cron")
    public Result<Map<String, Object>> validateCron(@RequestBody Map<String, String> params) {
        String cronExpression = params.get("cronExpression");
        boolean isValid = taskConfigService.validateCron(cronExpression);
        Map<String, Object> result = new java.util.HashMap<>();
        result.put("valid", isValid);
        if (!isValid) {
            result.put("message", com.task.scheduler.util.CronValidator.validateCronWithMessage(cronExpression));
        }
        return Result.success(result);
    }
}
