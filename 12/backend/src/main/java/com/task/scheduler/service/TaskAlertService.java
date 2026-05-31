package com.task.scheduler.service;

import com.task.scheduler.entity.TaskAlert;
import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.repository.TaskAlertRepository;
import com.task.scheduler.repository.TaskConfigRepository;
import com.task.scheduler.repository.TaskLogRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
public class TaskAlertService {

    private static final Logger logger = LoggerFactory.getLogger(TaskAlertService.class);

    @Autowired
    private TaskAlertRepository taskAlertRepository;

    @Autowired
    private TaskConfigRepository taskConfigRepository;

    @Autowired
    private TaskLogRepository taskLogRepository;

    @Value("${task.scheduler.alert.max-duration:300}")
    private long maxDuration;

    @Value("${task.scheduler.alert.consecutive-failures:3}")
    private int consecutiveFailures;

    public void checkAndCreateAlerts() {
        List<TaskConfig> tasks = taskConfigRepository.findActiveTasks();
        for (TaskConfig task : tasks) {
            checkTaskExecution(task);
            checkTaskDuration(task);
            checkConsecutiveFailures(task);
        }
    }

    private void checkTaskExecution(TaskConfig task) {
        if (task.getLastExecuteTime() == null) {
            return;
        }

        LocalDateTime expectedNextTime = calculateNextExecutionTime(task);
        if (expectedNextTime != null && LocalDateTime.now().isAfter(expectedNextTime.plusMinutes(5))) {
            createAlert(task, "MISSED_EXECUTION", 2,
                    "任务执行超时",
                    String.format("任务 %s 预期执行时间已过5分钟但未执行", task.getTaskName()));
        }
    }

    private LocalDateTime calculateNextExecutionTime(TaskConfig task) {
        if (task.getLastExecuteTime() == null || task.getCronExpression() == null) {
            return null;
        }

        try {
            org.quartz.CronExpression cron = new org.quartz.CronExpression(task.getCronExpression());
            java.util.Date nextDate = cron.getNextValidTimeAfter(java.sql.Timestamp.valueOf(task.getLastExecuteTime()));
            if (nextDate != null) {
                return nextDate.toInstant().atZone(java.time.ZoneId.systemDefault()).toLocalDateTime();
            }
        } catch (Exception e) {
            logger.error("Error calculating next execution time for task {}: {}", task.getTaskName(), e.getMessage());
        }
        return null;
    }

    private void checkTaskDuration(TaskConfig task) {
        List<TaskLog> recentLogs = taskLogRepository.findByTaskIdOrderByCreateTimeDesc(task.getId(), org.springframework.data.domain.PageRequest.of(0, 1)).getContent();

        if (!recentLogs.isEmpty()) {
            TaskLog recentLog = recentLogs.get(0);
            if (recentLog.getDuration() != null && recentLog.getDuration() > maxDuration) {
                createAlert(task, "LONG_DURATION", 1,
                        "任务执行耗时过长",
                        String.format("任务 %s 执行耗时 %d 秒，超过阈值 %d 秒",
                                task.getTaskName(), recentLog.getDuration(), maxDuration));
            }
        }
    }

    private void checkConsecutiveFailures(TaskConfig task) {
        List<TaskLog> recentLogs = taskLogRepository.findByTaskIdOrderByCreateTimeDesc(task.getId(), org.springframework.data.domain.PageRequest.of(0, consecutiveFailures)).getContent();

        long failedCount = recentLogs.stream()
                .filter(log -> log.getExecuteStatus() == 0)
                .count();

        if (failedCount >= consecutiveFailures) {
            createAlert(task, "CONSECUTIVE_FAILURE", 3,
                    "任务连续执行失败",
                    String.format("任务 %s 已连续失败 %d 次", task.getTaskName(), failedCount));
        }
    }

    public void createAlert(TaskConfig task, String alertType, Integer alertLevel, String message, String detail) {
        List<TaskAlert> existingAlerts = taskAlertRepository.findByTaskIdAndStatus(task.getId(), 0);
        boolean hasExisting = existingAlerts.stream()
                .anyMatch(alert -> alert.getAlertType().equals(alertType));

        if (hasExisting) {
            return;
        }

        TaskAlert alert = new TaskAlert();
        alert.setTaskId(task.getId());
        alert.setTaskName(task.getTaskName());
        alert.setAlertType(alertType);
        alert.setAlertLevel(alertLevel);
        alert.setAlertMessage(message);
        alert.setAlertDetail(detail);
        alert.setStatus(0);
        alert.setCreateTime(LocalDateTime.now());

        taskAlertRepository.save(alert);
        logger.warn("Alert created: {} - {}", alertType, message);
    }

    public List<TaskAlert> getActiveAlerts() {
        return taskAlertRepository.findByStatus(0);
    }

    public org.springframework.data.domain.Page<TaskAlert> getAlertHistory(Integer status, Integer alertLevel, int page, int size) {
        org.springframework.data.domain.Pageable pageable = org.springframework.data.domain.PageRequest.of(page, size);

        if (status != null) {
            return taskAlertRepository.findByStatusOrderByCreateTimeDesc(status, pageable);
        }

        if (alertLevel != null) {
            return taskAlertRepository.findByAlertLevel(alertLevel, pageable);
        }

        return taskAlertRepository.findAllByOrderByCreateTimeDesc(pageable);
    }

    public TaskAlert handleAlert(Long alertId, String handleBy, String handleRemark) {
        TaskAlert alert = taskAlertRepository.findById(alertId).orElse(null);
        if (alert == null) {
            return null;
        }

        alert.setStatus(1);
        alert.setHandleTime(LocalDateTime.now());
        alert.setHandleBy(handleBy);
        alert.setHandleRemark(handleRemark);

        return taskAlertRepository.save(alert);
    }

    public Long countActiveAlerts() {
        return taskAlertRepository.countByStatus(0);
    }

    public Long countAlertsByLevel(Integer level) {
        return taskAlertRepository.countByAlertLevelAndStatus(level, 0);
    }

    @Scheduled(fixedRate = 300000)
    public void scheduledAlertCheck() {
        logger.info("Starting scheduled alert check...");
        checkAndCreateAlerts();
        logger.info("Scheduled alert check completed");
    }
}
