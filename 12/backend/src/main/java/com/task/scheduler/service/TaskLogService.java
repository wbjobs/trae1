package com.task.scheduler.service;

import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.repository.TaskLogRepository;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
public class TaskLogService {

    @Autowired
    private TaskLogRepository taskLogRepository;

    public Page<TaskLog> getTaskLogs(Long taskId, int page, int size) {
        Pageable pageable = PageRequest.of(page, size);
        if (taskId != null) {
            return taskLogRepository.findByTaskIdOrderByCreateTimeDesc(taskId, pageable);
        }
        return taskLogRepository.findAllByOrderByCreateTimeDesc(pageable);
    }

    public TaskLog getTaskLogById(Long id) {
        return taskLogRepository.findById(id).orElse(null);
    }

    public List<TaskLog> getFailedTasksForRetry(int maxRetryAttempts) {
        return taskLogRepository.findFailedTasksForRetry(maxRetryAttempts);
    }

    public Long countTaskExecutions(Long taskId, LocalDateTime startTime, LocalDateTime endTime) {
        return taskLogRepository.countByTaskIdAndTimeRange(taskId, startTime, endTime);
    }

    public void deleteTaskLog(Long id) {
        taskLogRepository.deleteById(id);
    }

    public Page<TaskLog> getAllLogs(int page, int size) {
        Pageable pageable = PageRequest.of(page, size);
        return taskLogRepository.findAllByOrderByCreateTimeDesc(pageable);
    }
}
