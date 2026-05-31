package com.rsocket.gateway.circuitbreaker;

import io.github.resilience4j.circuitbreaker.CircuitBreaker;
import io.github.resilience4j.circuitbreaker.CircuitBreakerRegistry;
import io.github.resilience4j.reactor.circuitbreaker.operator.CircuitBreakerOperator;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

@Slf4j
@Service
public class CircuitBreakerService {

    private final CircuitBreakerRegistry circuitBreakerRegistry;

    public CircuitBreakerService(CircuitBreakerRegistry circuitBreakerRegistry) {
        this.circuitBreakerRegistry = circuitBreakerRegistry;
    }

    public <T> Mono<T> executeWithCircuitBreaker(String serviceName, Mono<T> mono) {
        CircuitBreaker circuitBreaker = getOrCreateCircuitBreaker(serviceName);
        return mono.transformDeferred(CircuitBreakerOperator.of(circuitBreaker))
                .doOnError(e -> log.error("Error in service {}: {}", serviceName, e.getMessage()));
    }

    public <T> Flux<T> executeWithCircuitBreaker(String serviceName, Flux<T> flux) {
        CircuitBreaker circuitBreaker = getOrCreateCircuitBreaker(serviceName);
        return flux.transformDeferred(CircuitBreakerOperator.of(circuitBreaker))
                .doOnError(e -> log.error("Error in service {}: {}", serviceName, e.getMessage()));
    }

    private CircuitBreaker getOrCreateCircuitBreaker(String serviceName) {
        return circuitBreakerRegistry.circuitBreaker(serviceName);
    }

    public CircuitBreaker.State getCircuitBreakerState(String serviceName) {
        return getOrCreateCircuitBreaker(serviceName).getState();
    }
}
