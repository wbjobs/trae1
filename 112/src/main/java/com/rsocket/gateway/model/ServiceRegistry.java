package com.rsocket.gateway.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ServiceRegistry {
    private String serviceName;
    private List<String> methods;
    private List<ServiceInstance> instances;
}
