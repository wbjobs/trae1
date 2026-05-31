package com.distributed.task.log;

import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.cloud.client.discovery.EnableDiscoveryClient;

@EnableDiscoveryClient
@MapperScan("com.distributed.task.log.mapper")
@SpringBootApplication
public class TaskLogApplication {

    public static void main(String[] args) {
        SpringApplication.run(TaskLogApplication.class, args);
    }
}
