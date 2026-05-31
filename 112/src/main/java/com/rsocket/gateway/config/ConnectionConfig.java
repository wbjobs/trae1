package com.rsocket.gateway.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

@Data
@Configuration
@ConfigurationProperties(prefix = "gateway.connection")
public class ConnectionConfig {

    private int maxPendingRequests = 1000;
    private int idleTimeoutMinutes = 5;
    private int keepaliveIntervalMinutes = 5;
    private int bufferSize = 100;
}
