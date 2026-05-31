package com.rsocket.gateway.handler;

import com.rsocket.gateway.model.ServiceInstance;
import com.rsocket.gateway.registry.ServiceRegistryManager;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.handler.annotation.MessageMapping;
import org.springframework.messaging.handler.annotation.Payload;
import org.springframework.stereotype.Controller;
import reactor.core.publisher.Mono;

import java.util.List;

@Slf4j
@Controller
@RequiredArgsConstructor
public class ServiceRegistrationHandler {

    private final ServiceRegistryManager serviceRegistryManager;

    @MessageMapping("register-service")
    public Mono<RegistrationResponse> registerService(@Payload RegistrationRequest request) {
        log.info("Received service registration request: {}", request);

        ServiceInstance instance = ServiceInstance.builder()
                .serviceName(request.getServiceName())
                .instanceId(request.getInstanceId())
                .host(request.getHost())
                .port(request.getPort())
                .weight(request.getWeight())
                .registerTime(System.currentTimeMillis())
                .healthy(true)
                .build();

        serviceRegistryManager.registerService(instance, request.getMethods());

        return Mono.just(RegistrationResponse.builder()
                .success(true)
                .message("Service registered successfully")
                .build());
    }

    @MessageMapping("unregister-service")
    public Mono<RegistrationResponse> unregisterService(@Payload UnregistrationRequest request) {
        log.info("Received service unregistration request: {}", request);
        serviceRegistryManager.unregisterService(request.getServiceName(), request.getInstanceId());
        return Mono.just(RegistrationResponse.builder()
                .success(true)
                .message("Service unregistered successfully")
                .build());
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class RegistrationRequest {
        private String serviceName;
        private String instanceId;
        private String host;
        private int port;
        private int weight;
        private List<String> methods;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class UnregistrationRequest {
        private String serviceName;
        private String instanceId;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class RegistrationResponse {
        private boolean success;
        private String message;
    }
}
