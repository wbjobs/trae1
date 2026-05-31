package com.rsocket.gateway.registry;

import com.rsocket.gateway.model.ServiceInstance;
import com.rsocket.gateway.model.ServiceRegistry;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Component;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Component
public class ServiceRegistryManager {

    private final Map<String, ServiceRegistry> registry = new ConcurrentHashMap<>();
    private final Map<String, Map<String, ServiceInstance>> serviceInstances = new ConcurrentHashMap<>();

    public void registerService(ServiceInstance instance, List<String> methods) {
        String serviceName = instance.getServiceName();
        String instanceId = instance.getInstanceId();

        registry.computeIfAbsent(serviceName, k -> ServiceRegistry.builder()
                .serviceName(serviceName)
                .methods(new ArrayList<>())
                .instances(new ArrayList<>())
                .build());

        ServiceRegistry serviceRegistry = registry.get(serviceName);
        
        methods.forEach(method -> {
            if (!serviceRegistry.getMethods().contains(method)) {
                serviceRegistry.getMethods().add(method);
            }
        });

        serviceInstances.computeIfAbsent(serviceName, k -> new ConcurrentHashMap<>());
        serviceInstances.get(serviceName).put(instanceId, instance);

        updateInstancesList(serviceName);
        log.info("Service registered: {} - {} - methods: {}", serviceName, instanceId, methods);
    }

    public void unregisterService(String serviceName, String instanceId) {
        if (serviceInstances.containsKey(serviceName)) {
            serviceInstances.get(serviceName).remove(instanceId);
            updateInstancesList(serviceName);
            log.info("Service unregistered: {} - {}", serviceName, instanceId);
        }
    }

    public ServiceRegistry getServiceRegistry(String serviceName) {
        return registry.get(serviceName);
    }

    public List<ServiceInstance> getServiceInstances(String serviceName) {
        ServiceRegistry serviceRegistry = registry.get(serviceName);
        return serviceRegistry != null ? serviceRegistry.getInstances() : Collections.emptyList();
    }

    public Collection<ServiceRegistry> getAllRegistries() {
        return registry.values();
    }

    private void updateInstancesList(String serviceName) {
        ServiceRegistry serviceRegistry = registry.get(serviceName);
        if (serviceRegistry != null) {
            Map<String, ServiceInstance> instances = serviceInstances.get(serviceName);
            serviceRegistry.setInstances(new ArrayList<>(instances.values()));
        }
    }
}
