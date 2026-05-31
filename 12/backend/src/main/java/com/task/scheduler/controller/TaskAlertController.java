package com.task.scheduler.controller;

import com.task.scheduler.common.Result;
import com.task.scheduler.entity.TaskAlert;
import com.task.scheduler.service.TaskAlertService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Page;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/alerts")
public class TaskAlertController {

    @Autowired
    private TaskAlertService taskAlertService;

    @GetMapping("/active")
    public Result<List<TaskAlert>> getActiveAlerts() {
        return Result.success(taskAlertService.getActiveAlerts());
    }

    @GetMapping
    public Result<Page<TaskAlert>> getAlertHistory(
            @RequestParam(required = false) Integer status,
            @RequestParam(required = false) Integer alertLevel,
            @RequestParam(defaultValue = "0") int page,
            @RequestParam(defaultValue = "10") int size) {
        return Result.success(taskAlertService.getAlertHistory(status, alertLevel, page, size));
    }

    @PostMapping("/{id}/handle")
    public Result<TaskAlert> handleAlert(
            @PathVariable Long id,
            @RequestParam String handleBy,
            @RequestParam(required = false) String handleRemark) {
        TaskAlert alert = taskAlertService.handleAlert(id, handleBy, handleRemark);
        if (alert == null) {
            return Result.error("Alert not found");
        }
        return Result.success(alert);
    }

    @GetMapping("/stats")
    public Result<Map<String, Object>> getAlertStats() {
        Map<String, Object> stats = new HashMap<>();
        stats.put("activeCount", taskAlertService.countActiveAlerts());
        stats.put("highCount", taskAlertService.countAlertsByLevel(3));
        stats.put("mediumCount", taskAlertService.countAlertsByLevel(2));
        stats.put("lowCount", taskAlertService.countAlertsByLevel(1));
        return Result.success(stats);
    }

    @PostMapping("/check")
    public Result<Void> triggerAlertCheck() {
        taskAlertService.checkAndCreateAlerts();
        return Result.success();
    }
}
