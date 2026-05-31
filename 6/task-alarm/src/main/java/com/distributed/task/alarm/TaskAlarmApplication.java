package com.distributed.task.alarm;

import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.cloud.client.discovery.EnableDiscoveryClient;

@EnableDiscoveryClient
@MapperScan("com.distributed.task.alarm.mapper")
@SpringBootApplication
public class TaskAlarmApplication {

    public static void main(String[] args) {
        SpringApplication.run(TaskAlarmApplication.class, args);
    }
}
