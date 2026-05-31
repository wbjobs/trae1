package com.distributed.task.retry;

import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.cloud.client.discovery.EnableDiscoveryClient;
import org.springframework.cloud.openfeign.EnableFeignClients;
import org.springframework.scheduling.annotation.EnableScheduling;

@EnableScheduling
@EnableDiscoveryClient
@EnableFeignClients(basePackages = "com.distributed.task.common.feign")
@MapperScan("com.distributed.task.retry.mapper")
@SpringBootApplication
public class TaskRetryApplication {

    public static void main(String[] args) {
        SpringApplication.run(TaskRetryApplication.class, args);
    }
}
