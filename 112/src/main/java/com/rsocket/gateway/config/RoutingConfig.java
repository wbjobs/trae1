package com.rsocket.gateway.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

import java.util.HashMap;
import java.util.Map;

@Data
@Configuration
@ConfigurationProperties(prefix = "gateway.routing")
public class RoutingConfig {

    private Map<String, RouteRule> rules = new HashMap<>();

    @Data
    public static class RouteRule {
        private String sourceService;
        private String sourceMethod;
        private String targetService;
        private String targetMethod;
        private boolean enabled = true;
    }
}
