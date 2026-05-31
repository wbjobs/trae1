package com.cdn.api;

import org.mybatis.spring.annotation.MapperScan;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication(scanBasePackages = "com.cdn")
@MapperScan("com.cdn.domain.mapper")
@EnableScheduling
public class CdnApiApplication {
    public static void main(String[] args) {
        SpringApplication.run(CdnApiApplication.class, args);
    }
}
