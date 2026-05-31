package com.rsocket.gateway.handler;

import com.rsocket.gateway.backpressure.BackpressureManager;
import com.rsocket.gateway.circuitbreaker.CircuitBreakerService;
import com.rsocket.gateway.connection.ConnectionTracker;
import com.rsocket.gateway.loadbalancer.WeightedRoundRobinLoadBalancer;
import com.rsocket.gateway.model.GatewayRequest;
import com.rsocket.gateway.model.ServiceInstance;
import com.rsocket.gateway.monitoring.MetricsService;
import com.rsocket.gateway.registry.RSocketConnectionManager;
import com.rsocket.gateway.registry.ServiceRegistryManager;
import com.rsocket.gateway.router.RoutingService;
import com.rsocket.gateway.scripting.MessageInterceptor;
import com.rsocket.gateway.scripting.model.InterceptContext;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.handler.annotation.MessageMapping;
import org.springframework.messaging.handler.annotation.Payload;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Controller;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.time.Duration;
import java.time.Instant;
import java.util.HashMap;
import java.util.UUID;

@Slf4j
@Controller
@RequiredArgsConstructor
public class GatewayHandler {

    private final ServiceRegistryManager serviceRegistryManager;
    private final WeightedRoundRobinLoadBalancer loadBalancer;
    private final RSocketConnectionManager connectionManager;
    private final CircuitBreakerService circuitBreakerService;
    private final MetricsService metricsService;
    private final RoutingService routingService;
    private final BackpressureManager backpressureManager;
    private final ConnectionTracker connectionTracker;
    private final MessageInterceptor messageInterceptor;

    @MessageMapping("request-response")
    public Mono<Object> requestResponse(@Payload GatewayRequest request) {
        GatewayRequest routed = routingService.applyRouting(request);
        String connectionId = generateConnectionId(routed);
        String route = routed.getServiceName() + "." + routed.getMethodName();
        String clientId = request.getRequestId() != null ? request.getRequestId() : "unknown";

        log.info("Request-Response: service={}, method={}, connection={}",
                routed.getServiceName(), routed.getMethodName(), connectionId);

        if (!backpressureManager.checkAndAcceptRequest(connectionId)) {
            return Mono.error(new IllegalStateException("Too many pending requests: " + connectionTracker.getMaxPendingRequests()));
        }

        return messageInterceptor.interceptRequestResponse(
                route,
                routed.getPayload(),
                new HashMap<>(),
                clientId,
                connectionId,
                ctx -> executeRequestWithInterception(ctx, routed, connectionId)
        )
        .doOnSuccess(response -> connectionTracker.decrementPending(connectionId))
        .doOnError(error -> connectionTracker.decrementPending(connectionId));
    }

    @MessageMapping("request-stream")
    public Flux<Object> requestStream(@Payload GatewayRequest request) {
        GatewayRequest routed = routingService.applyRouting(request);
        String connectionId = generateConnectionId(routed);
        String route = routed.getServiceName() + "." + routed.getMethodName();
        String clientId = request.getRequestId() != null ? request.getRequestId() : "unknown";

        log.info("Request-Stream: service={}, method={}, connection={}",
                routed.getServiceName(), routed.getMethodName(), connectionId);

        if (!backpressureManager.checkAndAcceptRequest(connectionId)) {
            return Flux.error(new IllegalStateException("Too many pending requests: " + connectionTracker.getMaxPendingRequests()));
        }

        return messageInterceptor.interceptRequestStream(
                route,
                routed.getPayload(),
                new HashMap<>(),
                clientId,
                connectionId,
                ctx -> executeStreamRequestWithInterception(ctx, routed, connectionId)
        )
        .doOnComplete(() -> connectionTracker.decrementPending(connectionId))
        .doOnError(error -> connectionTracker.decrementPending(connectionId));
    }

    @MessageMapping("fire-and-forget")
    public Mono<Void> fireAndForget(@Payload GatewayRequest request) {
        GatewayRequest routed = routingService.applyRouting(request);
        String connectionId = generateConnectionId(routed);
        String route = routed.getServiceName() + "." + routed.getMethodName();
        String clientId = request.getRequestId() != null ? request.getRequestId() : "unknown";

        log.info("Fire-and-Forget: service={}, method={}, connection={}",
                routed.getServiceName(), routed.getMethodName(), connectionId);

        if (!backpressureManager.checkAndAcceptRequest(connectionId)) {
            return Mono.error(new IllegalStateException("Too many pending requests: " + connectionTracker.getMaxPendingRequests()));
        }

        return messageInterceptor.interceptFireAndForget(
                route,
                routed.getPayload(),
                new HashMap<>(),
                clientId,
                connectionId,
                ctx -> executeFireAndForgetWithInterception(ctx, routed, connectionId)
        )
        .doOnSuccess(response -> connectionTracker.decrementPending(connectionId))
        .doOnError(error -> connectionTracker.decrementPending(connectionId));
    }

    @MessageMapping("channel")
    public Flux<Object> channel(@Payload Flux<GatewayRequest> requests) {
        String channelConnectionId = UUID.randomUUID().toString();
        log.info("Channel communication started: {}", channelConnectionId);

        return messageInterceptor.interceptChannel(
                "channel",
                requests.cast(Object.class),
                new HashMap<>(),
                "channel-client",
                channelConnectionId,
                ctx -> {
                    GatewayRequest request = (GatewayRequest) ctx.getPayload();
                    GatewayRequest routed = routingService.applyRouting(request);
                    String requestConnectionId = channelConnectionId + "-" + UUID.randomUUID().toString().substring(0, 8);

                    log.info("Channel request: service={}, method={}, connection={}",
                            routed.getServiceName(), routed.getMethodName(), requestConnectionId);

                    if (!backpressureManager.checkAndAcceptRequest(requestConnectionId)) {
                        return Flux.error(new IllegalStateException(
                                "Too many pending requests: " + connectionTracker.getMaxPendingRequests()));
                    }

                    return routeRequest(routed, "channel", requestConnectionId)
                            .doOnSuccess(response -> connectionTracker.decrementPending(requestConnectionId))
                            .doOnError(error -> connectionTracker.decrementPending(requestConnectionId))
                            .flux();
                }
        )
        .doFinally(signal -> {
            log.info("Channel communication ended: {}, signal: {}", channelConnectionId, signal);
        });
    }

    private Mono<Object> executeRequestWithInterception(InterceptContext ctx,
                                                        GatewayRequest request,
                                                        String connectionId) {
        Object effectivePayload = ctx.isModified() ? ctx.getPayload() : request.getPayload();
        GatewayRequest effectiveRequest = request.toBuilder()
                .payload(effectivePayload)
                .build();

        return routeRequest(effectiveRequest, "request-response", connectionId);
    }

    private Flux<Object> executeStreamRequestWithInterception(InterceptContext ctx,
                                                              GatewayRequest request,
                                                              String connectionId) {
        Object effectivePayload = ctx.isModified() ? ctx.getPayload() : request.getPayload();
        GatewayRequest effectiveRequest = request.toBuilder()
                .payload(effectivePayload)
                .build();

        return routeStreamRequest(effectiveRequest, "request-stream", connectionId);
    }

    private Mono<Void> executeFireAndForgetWithInterception(InterceptContext ctx,
                                                            GatewayRequest request,
                                                            String connectionId) {
        Object effectivePayload = ctx.isModified() ? ctx.getPayload() : request.getPayload();
        GatewayRequest effectiveRequest = request.toBuilder()
                .payload(effectivePayload)
                .build();

        return routeFireAndForget(effectiveRequest, connectionId);
    }

    private Mono<Object> routeRequest(GatewayRequest request, String interactionType, String connectionId) {
        ServiceInstance instance = selectInstance(request.getServiceName());
        return connectionManager.getConnection(instance)
                .flatMap(requester -> executeRequest(requester, request, interactionType,
                        instance.getServiceName(), connectionId));
    }

    private Flux<Object> routeStreamRequest(GatewayRequest request, String interactionType, String connectionId) {
        ServiceInstance instance = selectInstance(request.getServiceName());
        return connectionManager.getConnection(instance)
                .flatMapMany(requester -> executeStreamRequest(requester, request, interactionType,
                        instance.getServiceName(), connectionId));
    }

    private Mono<Void> routeFireAndForget(GatewayRequest request, String connectionId) {
        ServiceInstance instance = selectInstance(request.getServiceName());
        return connectionManager.getConnection(instance)
                .flatMap(requester -> executeFireAndForget(requester, request,
                        instance.getServiceName(), connectionId));
    }

    private ServiceInstance selectInstance(String serviceName) {
        var instances = serviceRegistryManager.getServiceInstances(serviceName);
        if (instances.isEmpty()) {
            throw new IllegalStateException("No service instances available for: " + serviceName);
        }
        return loadBalancer.select(serviceName, instances);
    }

    private Mono<Object> executeRequest(RSocketRequester requester, GatewayRequest request,
                                         String interactionType, String serviceName, String connectionId) {
        Instant startTime = Instant.now();
        String route = buildRoute(request.getMethodName());
        Mono<Object> result = requester.route(route)
                .data(request.getPayload())
                .retrieveMono(Object.class);

        return circuitBreakerService.executeWithCircuitBreaker(serviceName,
                        backpressureManager.propagateBackpressureMono(connectionId, result))
                .doOnSuccess(response -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, true);
                    connectionTracker.updateActivity(connectionId);
                })
                .doOnError(error -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, false);
                    connectionTracker.updateActivity(connectionId);
                });
    }

    private Flux<Object> executeStreamRequest(RSocketRequester requester, GatewayRequest request,
                                               String interactionType, String serviceName, String connectionId) {
        Instant startTime = Instant.now();
        String route = buildRoute(request.getMethodName());
        Flux<Object> result = requester.route(route)
                .data(request.getPayload())
                .retrieveFlux(Object.class);

        return circuitBreakerService.executeWithCircuitBreaker(serviceName,
                        backpressureManager.propagateBackpressure(connectionId, result,
                                n -> log.debug("Forwarding Request N({}) to backend for {}", n, connectionId)))
                .doOnComplete(() -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, true);
                    connectionTracker.updateActivity(connectionId);
                })
                .doOnError(error -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, false);
                    connectionTracker.updateActivity(connectionId);
                });
    }

    private Mono<Void> executeFireAndForget(RSocketRequester requester, GatewayRequest request,
                                             String serviceName, String connectionId) {
        Instant startTime = Instant.now();
        String route = buildRoute(request.getMethodName());
        Mono<Void> result = requester.route(route)
                .data(request.getPayload())
                .send();

        return circuitBreakerService.executeWithCircuitBreaker(serviceName, result)
                .doOnSuccess(response -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, true);
                    connectionTracker.updateActivity(connectionId);
                })
                .doOnError(error -> {
                    recordMetrics(serviceName, request.getMethodName(), startTime, false);
                    connectionTracker.updateActivity(connectionId);
                });
    }

    private String generateConnectionId(GatewayRequest request) {
        String base = request.getServiceName() + "-" + request.getMethodName();
        if (request.getRequestId() != null) {
            return base + "-" + request.getRequestId();
        }
        return base + "-" + UUID.randomUUID().toString().substring(0, 8);
    }

    private void recordMetrics(String serviceName, String methodName, Instant startTime, boolean success) {
        Duration latency = Duration.between(startTime, Instant.now());
        metricsService.recordRequest(serviceName, methodName, latency, success);
    }

    private String buildRoute(String methodName) {
        return methodName;
    }
}
