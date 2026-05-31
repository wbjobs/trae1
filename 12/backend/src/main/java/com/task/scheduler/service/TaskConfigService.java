package com.task.scheduler.service;

import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.repository.TaskConfigRepository;
import com.task.scheduler.util.CronValidator;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
public class TaskConfigService {

    @Autowired
    private TaskConfigRepository taskConfigRepository;

    public List<TaskConfig> getAllTasks() {
        return taskConfigRepository.findByDeleted(0);
    }

    public TaskConfig getTaskById(Long id) {
        return taskConfigRepository.findById(id).orElse(null);
    }

    public TaskConfig createTask(TaskConfig taskConfig) {
        String cronError = CronValidator.validateCronWithMessage(taskConfig.getCronExpression());
        if (cronError != null) {
            throw new RuntimeException("Cron表达式无效: " + cronError);
        }

        TaskConfig existing = taskConfigRepository.findByTaskNameAndDeleted(taskConfig.getTaskName(), 0);
        if (existing != null) {
            throw new RuntimeException("任务名称已存在: " + taskConfig.getTaskName());
        }

        taskConfig.setCreateTime(LocalDateTime.now());
        taskConfig.setUpdateTime(LocalDateTime.now());
        taskConfig.setStatus(0);
        taskConfig.setDeleted(0);
        return taskConfigRepository.save(taskConfig);
    }

    public TaskConfig updateTask(Long id, TaskConfig taskConfig) {
        TaskConfig existing = taskConfigRepository.findById(id).orElse(null);
        if (existing == null) {
            return null;
        }

        String cronError = CronValidator.validateCronWithMessage(taskConfig.getCronExpression());
        if (cronError != null) {
            throw new RuntimeException("Cron表达式无效: " + cronError);
        }

        if (!existing.getTaskName().equals(taskConfig.getTaskName())) {
            TaskConfig nameCheck = taskConfigRepository.findByTaskNameAndDeleted(taskConfig.getTaskName(), 0);
            if (nameCheck != null) {
                throw new RuntimeException("任务名称已存在: " + taskConfig.getTaskName());
            }
        }

        existing.setTaskName(taskConfig.getTaskName());
        existing.setTaskGroup(taskConfig.getTaskGroup());
        existing.setCronExpression(taskConfig.getCronExpression());
        existing.setTaskType(taskConfig.getTaskType());
        existing.setTaskParams(taskConfig.getTaskParams());
        existing.setTargetServer(taskConfig.getTargetServer());
        existing.setExecuteCommand(taskConfig.getExecuteCommand());
        existing.setDescription(taskConfig.getDescription());
        existing.setRetryCount(taskConfig.getRetryCount());
        existing.setRetryInterval(taskConfig.getRetryInterval());
        existing.setTimeout(taskConfig.getTimeout());
        existing.setUpdateTime(LocalDateTime.now());
        return taskConfigRepository.save(existing);
    }

    public void deleteTask(Long id) {
        TaskConfig taskConfig = taskConfigRepository.findById(id).orElse(null);
        if (taskConfig != null) {
            taskConfig.setDeleted(1);
            taskConfig.setUpdateTime(LocalDateTime.now());
            taskConfigRepository.save(taskConfig);
        }
    }

    public List<TaskConfig> getTasksByServer(String serverName) {
        return taskConfigRepository.findByTargetServer(serverName);
    }

    public boolean validateCron(String cronExpression) {
        return CronValidator.isValidCron(cronExpression);
    }
}
