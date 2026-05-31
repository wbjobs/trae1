package com.clickstream;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableScheduling
public class ClickStreamSessionizationApplication {

    public static void main(String[] args) {
        SpringApplication.run(ClickStreamSessionizationApplication.class, args);
    }
}
