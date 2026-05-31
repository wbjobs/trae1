package com.rsocket.gateway.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ServiceInstance {
    private String serviceName;
    private String instanceId;
    private String host;
    private int port;
    private int weight;
    private long registerTime;
    private boolean healthy;
}
