package com.rsocket.gateway.handler;

import com.rsocket.gateway.backpressure.BackpressureManager;
import com.rsocket.gateway.connection.ConnectionInfo;
import com.rsocket.gateway.connection.ConnectionTracker;
import com.rsocket.gateway.connection.KeepaliveManager;
import com.rsocket.gateway.loadbalancer.WeightedRoundRobinLoadBalancer;
import com.rsocket.gateway.model.GatewayRequest;
import com.rsocket.gateway.model.ServiceInstance;
import com.rsocket.gateway.registry.RSocketConnectionManager;
import com.rsocket.gateway.registry.ServiceRegistryManager;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.handler.annotation.MessageMapping;
import org.springframework.messaging.handler.annotation.Payload;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Controller;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;
import reactor.core.publisher.Sinks;

import java.util.UUID;

@Slf4j
@Controller
@RequiredArgsConstructor
public class SafeChannelHandler {

    private final ServiceRegistryManager serviceRegistryManager;
    private final WeightedRoundRobinLoadBalancer loadBalancer;
    private final RSocketConnectionManager connectionManager;
    private final BackpressureManager backpressureManager;
    private final ConnectionTracker connectionTracker;
    private final KeepaliveManager keepaliveManager;

    @MessageMapping("secure-channel")
    public Flux<Object> secureChannel(@Payload Flux<GatewayRequest> requests) {
        String connectionId = UUID.randomUUID().toString();
        log.info("Secure channel opened: {}", connectionId);

        Sinks.Many<Object> responseSink = Sinks.many().multicast().onBackpressureBuffer();

        return requests
                .flatMap(request -> {
                    log.info("Secure channel request: service={}, method={}, connection={}",
                            request.getServiceName(), request.getMethodName(), connectionId);

                    if (!backpressureManager.checkAndAcceptRequest(connectionId)) {
                        responseSink.tryEmitError(new IllegalStateException(
                                "Too many pending requests: " + connectionTracker.getMaxPendingRequests()));
                        return Flux.empty();
                    }

                    return handleRequest(connectionId, request, responseSink);
                })
                .doOnSubscribe(sub -> {
                    log.debug("Secure channel subscribed: {}", connectionId);
                })
                .doFinally(signal -> {
                    log.info("Secure channel closed: {}, signal: {}", connectionId, signal);
                    connectionTracker.unregisterConnection(connectionId);
                });
    }

    private Flux<Object> handleRequest(String connectionId, GatewayRequest request,
                                       Sinks.Many<Object> responseSink) {
        try {
            ServiceInstance instance = selectInstance(request.getServiceName());

            return connectionManager.getConnection(instance)
                    .flatMapMany(requester -> {
                        ConnectionInfo info = connectionTracker.registerConnection(
                                connectionId,
                                request.getRequestId() != null ? request.getRequestId() : "unknown",
                                instance.getServiceName(),
                                requester
                        );

                        Flux<Object> upstream = requester.route(request.getMethodName())
                                .data(request.getPayload())
                                .retrieveFlux(Object.class);

                        return backpressureManager.propagateBackpressure(
                                connectionId,
                                upstream,
                                n -> {
                                    log.debug("Forwarding request N({}) to backend for connection {}", n, connectionId);
                                }
                        );
                    })
                    .doOnError(error -> {
                        log.error("Error in secure channel {}: {}", connectionId, error.getMessage());
                        connectionTracker.decrementPending(connectionId);
                    })
                    .doOnComplete(() -> {
                        log.debug("Request completed for connection {}", connectionId);
                        connectionTracker.decrementPending(connectionId);
                    });
        } catch (Exception e) {
            log.error("Failed to handle request for connection {}: {}", connectionId, e.getMessage());
            connectionTracker.decrementPending(connectionId);
            return Flux.error(e);
        }
    }

    private ServiceInstance selectInstance(String serviceName) {
        var instances = serviceRegistryManager.getServiceInstances(serviceName);
        if (instances.isEmpty()) {
            throw new IllegalStateException("No service instances available for: " + serviceName);
        }
        return loadBalancer.select(serviceName, instances);
    }
}
