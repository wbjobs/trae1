package com.rsocket.gateway.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder(toBuilder = true)
@NoArgsConstructor
@AllArgsConstructor
public class GatewayRequest {
    private String serviceName;
    private String methodName;
    private Object payload;
    private String requestId;
    private long timestamp;
}
