package com.rsocket.gateway.loadbalancer;

import com.rsocket.gateway.model.ServiceInstance;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

@Slf4j
@Component
public class WeightedRoundRobinLoadBalancer {

    private final ConcurrentHashMap<String, AtomicInteger> counters = new ConcurrentHashMap<>();

    public ServiceInstance select(String serviceName, List<ServiceInstance> instances) {
        if (instances == null || instances.isEmpty()) {
            throw new IllegalArgumentException("No service instances available for: " + serviceName);
        }

        int totalWeight = instances.stream()
                .filter(ServiceInstance::isHealthy)
                .mapToInt(ServiceInstance::getWeight)
                .sum();

        if (totalWeight == 0) {
            throw new IllegalStateException("No healthy service instances available for: " + serviceName);
        }

        AtomicInteger counter = counters.computeIfAbsent(serviceName, k -> new AtomicInteger(0));
        int current = Math.abs(counter.getAndIncrement() % totalWeight);

        for (ServiceInstance instance : instances) {
            if (!instance.isHealthy()) {
                continue;
            }
            current -= instance.getWeight();
            if (current < 0) {
                log.debug("Selected instance: {} for service: {}", instance.getInstanceId(), serviceName);
                return instance;
            }
        }

        return instances.get(0);
    }
}
