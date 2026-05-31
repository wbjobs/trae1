package com.rsocket.gateway.controller;

import com.rsocket.gateway.connection.ConnectionTracker;
import com.rsocket.gateway.monitoring.MetricsService;
import com.rsocket.gateway.registry.ServiceRegistryManager;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;
import reactor.core.publisher.Mono;

@RestController
@RequestMapping("/api/dashboard")
@RequiredArgsConstructor
public class DashboardController {

    private final MetricsService metricsService;
    private final ServiceRegistryManager serviceRegistryManager;
    private final ConnectionTracker connectionTracker;

    @GetMapping("/metrics")
    public Mono<MetricsService.DashboardMetrics> getDashboardMetrics() {
        return Mono.just(metricsService.getDashboardMetrics());
    }

    @GetMapping("/services")
    public Mono<?> getRegisteredServices() {
        return Mono.just(serviceRegistryManager.getAllRegistries());
    }

    @GetMapping("/services/{serviceName}")
    public Mono<?> getServiceDetails(@PathVariable String serviceName) {
        return Mono.justOrEmpty(serviceRegistryManager.getServiceRegistry(serviceName));
    }

    @GetMapping("/services/{serviceName}/metrics")
    public Mono<?> getServiceMetrics(@PathVariable String serviceName) {
        return Mono.justOrEmpty(metricsService.getServiceMetrics(serviceName));
    }

    @GetMapping("/debug-mem")
    public Mono<ConnectionTracker.ConnectionMemorySnapshot> getDebugMemory() {
        return Mono.just(connectionTracker.getMemorySnapshot());
    }

    @GetMapping("/debug-mem/{connectionId}")
    public Mono<?> getConnectionMemory(@PathVariable String connectionId) {
        return Mono.justOrEmpty(connectionTracker.getConnection(connectionId));
    }
}
