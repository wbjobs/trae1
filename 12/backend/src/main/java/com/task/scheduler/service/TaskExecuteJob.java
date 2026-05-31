package com.task.scheduler.service;

import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.repository.TaskConfigRepository;
import org.quartz.Job;
import org.quartz.JobDataMap;
import org.quartz.JobExecutionContext;
import org.quartz.JobExecutionException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

@Component
public class TaskExecuteJob implements Job {

    private static final Logger logger = LoggerFactory.getLogger(TaskExecuteJob.class);

    @Autowired
    private TaskConfigRepository taskConfigRepository;

    @Autowired
    private TaskSchedulerService taskSchedulerService;

    @Override
    public void execute(JobExecutionContext context) throws JobExecutionException {
        JobDataMap dataMap = context.getJobDetail().getJobDataMap();
        Long taskId = dataMap.getLong("taskId");

        TaskConfig taskConfig = taskConfigRepository.findById(taskId).orElse(null);
        if (taskConfig == null || taskConfig.getStatus() != 1) {
            logger.warn("Task {} is not active or not found, skipping", taskId);
            return;
        }

        logger.info("Executing scheduled task: {}", taskConfig.getTaskName());
        taskSchedulerService.executeTask(taskConfig);
    }
}
