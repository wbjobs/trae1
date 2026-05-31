package com.task.scheduler.service;

import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.repository.TaskConfigRepository;
import com.task.scheduler.repository.TaskLogRepository;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Service
public class TaskStatsService {

    @Autowired
    private TaskLogRepository taskLogRepository;

    @Autowired
    private TaskConfigRepository taskConfigRepository;

    public Map<String, Object> getTaskStats(Long taskId, LocalDateTime startTime, LocalDateTime endTime) {
        Map<String, Object> stats = new HashMap<>();

        List<TaskLog> logs;
        if (taskId != null) {
            logs = taskLogRepository.findByTaskIdAndExecuteStatus(taskId, 2);
        } else {
            logs = taskLogRepository.findAll().stream()
                    .filter(log -> log.getExecuteStatus() == 2)
                    .collect(java.util.stream.Collectors.toList());
        }

        if (startTime != null && endTime != null) {
            logs = logs.stream()
                    .filter(log -> log.getStartTime() != null
                            && !log.getStartTime().isBefore(startTime)
                            && !log.getStartTime().isAfter(endTime))
                    .collect(java.util.stream.Collectors.toList());
        }

        long totalExecutions = logs.size();
        double avgDuration = logs.stream()
                .filter(log -> log.getDuration() != null)
                .mapToLong(TaskLog::getDuration)
                .average()
                .orElse(0);
        long maxDuration = logs.stream()
                .filter(log -> log.getDuration() != null)
                .mapToLong(TaskLog::getDuration)
                .max()
                .orElse(0);
        long minDuration = logs.stream()
                .filter(log -> log.getDuration() != null)
                .mapToLong(TaskLog::getDuration)
                .min()
                .orElse(0);

        long successCount = logs.stream()
                .filter(log -> log.getExecuteStatus() == 2)
                .count();
        long failedCount = logs.stream()
                .filter(log -> log.getExecuteStatus() == 0)
                .count();

        stats.put("totalExecutions", totalExecutions);
        stats.put("successCount", successCount);
        stats.put("failedCount", failedCount);
        stats.put("successRate", totalExecutions > 0 ? (double) successCount / totalExecutions * 100 : 0);
        stats.put("avgDuration", Math.round(avgDuration * 100.0) / 100.0);
        stats.put("maxDuration", maxDuration);
        stats.put("minDuration", minDuration);

        return stats;
    }

    public Map<String, Object> getOverallStats() {
        Map<String, Object> stats = new HashMap<>();

        List<TaskConfig> tasks = taskConfigRepository.findByDeleted(0);
        List<TaskLog> logs = taskLogRepository.findAll();

        stats.put("totalTasks", tasks.size());
        stats.put("runningTasks", tasks.stream().filter(t -> t.getStatus() == 1).count());
        stats.put("stoppedTasks", tasks.stream().filter(t -> t.getStatus() == 0).count());
        stats.put("totalExecutions", logs.size());
        stats.put("successCount", logs.stream().filter(l -> l.getExecuteStatus() == 2).count());
        stats.put("failedCount", logs.stream().filter(l -> l.getExecuteStatus() == 0).count());
        stats.put("runningCount", logs.stream().filter(l -> l.getExecuteStatus() == 1).count());

        return stats;
    }

    public Map<String, Object> getServerTaskDistribution() {
        Map<String, Object> stats = new HashMap<>();
        List<TaskConfig> tasks = taskConfigRepository.findByDeleted(0);

        Map<String, Long> serverCounts = tasks.stream()
                .collect(java.util.stream.Collectors.groupingBy(
                        TaskConfig::getTargetServer,
                        java.util.stream.Collectors.counting()
                ));

        stats.put("serverDistribution", serverCounts);
        return stats;
    }
}
