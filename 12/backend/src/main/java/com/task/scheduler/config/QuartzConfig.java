package com.task.scheduler.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.scheduling.quartz.SchedulerFactoryBean;

import javax.sql.DataSource;
import java.util.Properties;

@Configuration
public class QuartzConfig {

    @Bean
    public SchedulerFactoryBean schedulerFactoryBean(DataSource dataSource) {
        SchedulerFactoryBean factory = new SchedulerFactoryBean();
        factory.setDataSource(dataSource);
        factory.setAutoStartup(true);
        factory.setStartupDelay(5);
        factory.setOverwriteExistingJobs(true);
        factory.setApplicationContextSchedulerContextKey("applicationContext");

        Properties props = new Properties();
        props.put("org.quartz.scheduler.instanceName", "TaskScheduler");
        props.put("org.quartz.scheduler.instanceId", "AUTO");
        props.put("org.quartz.jobStore.class", "org.springframework.scheduling.quartz.LocalDataSourceJobStore");
        props.put("org.quartz.jobStore.isClustered", "true");
        props.put("org.quartz.jobStore.clusterCheckinInterval", "15000");
        props.put("org.quartz.jobStore.misfireThreshold", "60000");
        props.put("org.quartz.threadPool.threadCount", "25");
        props.put("org.quartz.threadPool.threadPriority", "5");
        factory.setQuartzProperties(props);

        return factory;
    }
}
