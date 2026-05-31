package com.rsocket.gateway.router;

import com.rsocket.gateway.config.RoutingConfig;
import com.rsocket.gateway.model.GatewayRequest;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

@Slf4j
@Service
@RequiredArgsConstructor
public class RoutingService {

    private final RoutingConfig routingConfig;

    public GatewayRequest applyRouting(GatewayRequest request) {
        String key = request.getServiceName() + "." + request.getMethodName();
        RoutingConfig.RouteRule rule = routingConfig.getRules().get(key);
        
        if (rule != null && rule.isEnabled()) {
            log.debug("Applying routing rule: {} -> {}.{}", key, 
                    rule.getTargetService(), rule.getTargetMethod());
            
            return GatewayRequest.builder()
                    .serviceName(rule.getTargetService())
                    .methodName(rule.getTargetMethod())
                    .payload(request.getPayload())
                    .requestId(request.getRequestId())
                    .timestamp(request.getTimestamp())
                    .build();
        }
        
        return request;
    }
}
