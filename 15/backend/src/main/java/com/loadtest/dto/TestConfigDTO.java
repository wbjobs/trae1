package com.loadtest.dto;

import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import lombok.Builder;

@Data
@NoArgsConstructor
@AllArgsConstructor
@Builder
public class TestConfigDTO {
    private Long id;
    private String name;
    private String description;
    private String method;
    private String url;
    private String headers;
    private String requestBody;
    private Integer threadCount;
    private Integer rampUpTime;
    private Integer loopCount;
    private Integer duration;
    private Boolean useLoopCount;
    private String protocol;
    private Integer port;
    private String path;
    private String domain;
    private String createdAt;
    private String updatedAt;
    private Boolean simulateDelay;
    private Integer delayMinMs;
    private Integer delayMaxMs;
    private Boolean simulateTimeout;
    private Double timeoutProbability;
    private Boolean simulateError;
    private Double errorProbability;
    private String errorStatusCodes;
    private Integer connectionTimeout;
    private Integer responseTimeout;
}
