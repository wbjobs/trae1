package com.task.scheduler.config;

import com.task.scheduler.service.TaskSchedulerService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.CommandLineRunner;
import org.springframework.stereotype.Component;

@Component
public class ApplicationInitializer implements CommandLineRunner {

    private static final Logger logger = LoggerFactory.getLogger(ApplicationInitializer.class);

    @Autowired
    private TaskSchedulerService taskSchedulerService;

    @Override
    public void run(String... args) {
        logger.info("Initializing scheduled tasks...");
        try {
            taskSchedulerService.initScheduledTasks();
            logger.info("Scheduled tasks initialized successfully");
        } catch (Exception e) {
            logger.error("Failed to initialize scheduled tasks", e);
        }
    }
}
