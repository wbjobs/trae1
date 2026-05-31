package com.task.scheduler.controller;

import com.task.scheduler.common.Result;
import com.task.scheduler.service.TaskStatsService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.web.bind.annotation.*;

import java.time.LocalDateTime;
import java.util.Map;

@RestController
@RequestMapping("/stats")
public class TaskStatsController {

    @Autowired
    private TaskStatsService taskStatsService;

    @GetMapping("/task/{taskId}")
    public Result<Map<String, Object>> getTaskStats(
            @PathVariable Long taskId,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime startTime,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime endTime) {
        return Result.success(taskStatsService.getTaskStats(taskId, startTime, endTime));
    }

    @GetMapping("/overall")
    public Result<Map<String, Object>> getOverallStats() {
        return Result.success(taskStatsService.getOverallStats());
    }

    @GetMapping("/server-distribution")
    public Result<Map<String, Object>> getServerTaskDistribution() {
        return Result.success(taskStatsService.getServerTaskDistribution());
    }
}
