package com.task.scheduler.service;

import com.task.scheduler.entity.ServerNode;
import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.repository.ServerNodeRepository;
import com.task.scheduler.repository.TaskConfigRepository;
import com.task.scheduler.repository.TaskLogRepository;
import org.quartz.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.List;

@Service
public class TaskSchedulerService {

    private static final Logger logger = LoggerFactory.getLogger(TaskSchedulerService.class);

    @Autowired
    private Scheduler scheduler;

    @Autowired
    private TaskConfigRepository taskConfigRepository;

    @Autowired
    private TaskLogRepository taskLogRepository;

    @Autowired
    private ServerNodeRepository serverNodeRepository;

    @Autowired
    private TaskExecuteService taskExecuteService;

    public void startTask(Long taskId) throws SchedulerException {
        TaskConfig taskConfig = taskConfigRepository.findById(taskId).orElse(null);
        if (taskConfig == null) {
            throw new RuntimeException("Task not found: " + taskId);
        }

        JobKey jobKey = new JobKey(taskConfig.getTaskName(), taskConfig.getTaskGroup());

        if (scheduler.checkExists(jobKey)) {
            scheduler.deleteJob(jobKey);
        }

        JobDetail jobDetail = JobBuilder.newJob(TaskExecuteJob.class)
                .withIdentity(taskConfig.getTaskName(), taskConfig.getTaskGroup())
                .usingJobData("taskId", taskConfig.getId())
                .storeDurably()
                .build();

        CronTrigger trigger = TriggerBuilder.newTrigger()
                .withIdentity(taskConfig.getTaskName() + "_trigger", taskConfig.getTaskGroup())
                .withSchedule(CronScheduleBuilder.cronSchedule(taskConfig.getCronExpression())
                        .inTimeZone(java.util.TimeZone.getTimeZone("Asia/Shanghai"))
                        .withMisfireHandlingInstructionFireAndProceed())
                .build();

        scheduler.scheduleJob(jobDetail, trigger);

        taskConfig.setStatus(1);
        taskConfig.setUpdateTime(LocalDateTime.now());
        taskConfigRepository.save(taskConfig);

        logger.info("Task started: {}", taskConfig.getTaskName());
    }

    public void stopTask(Long taskId) throws SchedulerException {
        TaskConfig taskConfig = taskConfigRepository.findById(taskId).orElse(null);
        if (taskConfig == null) {
            throw new RuntimeException("Task not found: " + taskId);
        }

        JobKey jobKey = new JobKey(taskConfig.getTaskName(), taskConfig.getTaskGroup());

        if (scheduler.checkExists(jobKey)) {
            scheduler.deleteJob(jobKey);
        }

        taskConfig.setStatus(0);
        taskConfig.setUpdateTime(LocalDateTime.now());
        taskConfigRepository.save(taskConfig);

        logger.info("Task stopped: {}", taskConfig.getTaskName());
    }

    public void executeTaskOnce(Long taskId) {
        TaskConfig taskConfig = taskConfigRepository.findById(taskId).orElse(null);
        if (taskConfig == null) {
            throw new RuntimeException("Task not found: " + taskId);
        }

        Thread thread = new Thread(() -> {
            TaskLog taskLog = taskExecuteService.executeTask(taskConfig);
            taskLogRepository.save(taskLog);
            taskConfigRepository.save(taskConfig);
        });
        thread.start();
    }

    public void executeTask(TaskConfig taskConfig) {
        TaskLog taskLog = taskExecuteService.executeTask(taskConfig);
        taskLogRepository.save(taskLog);
        taskConfigRepository.save(taskConfig);
    }

    public void distributeTaskToServers(Long taskId, List<String> serverNames) {
        TaskConfig taskConfig = taskConfigRepository.findById(taskId).orElse(null);
        if (taskConfig == null) {
            throw new RuntimeException("Task not found: " + taskId);
        }

        for (String serverName : serverNames) {
            ServerNode serverNode = serverNodeRepository.findByServerNameAndDeleted(serverName, 0);
            if (serverNode != null && serverNode.getStatus() == 1) {
                String distributedTaskName = taskConfig.getTaskName() + "_" + serverName;
                
                TaskConfig existingTask = taskConfigRepository.findByTaskNameAndDeleted(distributedTaskName, 0);
                if (existingTask != null) {
                    logger.warn("Task {} already exists for server {}, skipping", distributedTaskName, serverName);
                    continue;
                }
                
                TaskConfig distributedTask = new TaskConfig();
                distributedTask.setTaskName(distributedTaskName);
                distributedTask.setTaskGroup(taskConfig.getTaskGroup());
                distributedTask.setCronExpression(taskConfig.getCronExpression());
                distributedTask.setTaskType(taskConfig.getTaskType());
                distributedTask.setTaskParams(taskConfig.getTaskParams());
                distributedTask.setTargetServer(serverName);
                distributedTask.setExecuteCommand(taskConfig.getExecuteCommand());
                distributedTask.setDescription(taskConfig.getDescription() + " - Distributed to " + serverName);
                distributedTask.setRetryCount(taskConfig.getRetryCount());
                distributedTask.setRetryInterval(taskConfig.getRetryInterval());
                distributedTask.setTimeout(taskConfig.getTimeout());
                distributedTask.setStatus(0);
                distributedTask.setCreateTime(LocalDateTime.now());
                distributedTask.setUpdateTime(LocalDateTime.now());

                taskConfigRepository.save(distributedTask);
                logger.info("Task distributed to server: {}", serverName);
            }
        }
    }

    public List<TaskConfig> getAllTasks() {
        return taskConfigRepository.findByDeleted(0);
    }

    public void initScheduledTasks() throws SchedulerException {
        List<TaskConfig> activeTasks = taskConfigRepository.findActiveTasks();
        for (TaskConfig taskConfig : activeTasks) {
            try {
                startTask(taskConfig.getId());
            } catch (Exception e) {
                logger.error("Failed to initialize task: {}", taskConfig.getTaskName(), e);
            }
        }
    }

    public void batchExecuteTasks(List<Long> taskIds) {
        for (Long taskId : taskIds) {
            try {
                executeTaskOnce(taskId);
            } catch (Exception e) {
                logger.error("Failed to execute task {}: {}", taskId, e.getMessage());
            }
        }
    }
}
