package com.alibaba.polardb.index.config;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.scheduling.annotation.EnableAsync;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;

import java.util.concurrent.Executor;
import java.util.concurrent.ThreadPoolExecutor;

@Configuration
@EnableAsync
public class AsyncConfig {

    @Bean(name = "canalListenerExecutor")
    public Executor canalListenerExecutor(GlobalIndexSyncProperties properties) {
        ThreadPoolTaskExecutor executor = new ThreadPoolTaskExecutor();
        int shardCount = properties.getShards() != null ? properties.getShards().size() : 3;
        executor.setCorePoolSize(shardCount);
        executor.setMaxPoolSize(shardCount * 2);
        executor.setQueueCapacity(100);
        executor.setThreadNamePrefix("canal-listener-");
        executor.setRejectedExecutionHandler(new ThreadPoolExecutor.CallerRunsPolicy());
        executor.initialize();
        return executor;
    }

    @Bean(name = "processorExecutor")
    public Executor processorExecutor(GlobalIndexSyncProperties properties) {
        ProcessorConfig config = properties.getProcessor();
        ThreadPoolTaskExecutor executor = new ThreadPoolTaskExecutor();
        executor.setCorePoolSize(config.getThreadPoolSize());
        executor.setMaxPoolSize(config.getThreadPoolSize() * 2);
        executor.setQueueCapacity(config.getBatchQueueCapacity());
        executor.setThreadNamePrefix("index-processor-");
        executor.setRejectedExecutionHandler(new ThreadPoolExecutor.CallerRunsPolicy());
        executor.initialize();
        return executor;
    }
}
