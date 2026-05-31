package com.rsocket.gateway.registry;

import com.rsocket.gateway.model.ServiceInstance;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Component;
import reactor.core.publisher.Mono;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Component
public class RSocketConnectionManager {

    private final Map<String, RSocketRequester> connections = new ConcurrentHashMap<>();
    private final RSocketRequester.Builder requesterBuilder;

    public RSocketConnectionManager(RSocketRequester.Builder requesterBuilder) {
        this.requesterBuilder = requesterBuilder;
    }

    public Mono<RSocketRequester> getConnection(ServiceInstance instance) {
        String key = getConnectionKey(instance);
        RSocketRequester existing = connections.get(key);
        if (existing != null) {
            return Mono.just(existing);
        }
        return createConnection(instance)
                .doOnNext(requester -> connections.put(key, requester));
    }

    private Mono<RSocketRequester> createConnection(ServiceInstance instance) {
        return requesterBuilder
                .tcp(instance.getHost(), instance.getPort())
                .reconnect(reactor.util.retry.Retry.backoff(3, java.time.Duration.ofSeconds(1)))
                .doOnError(e -> log.error("Failed to connect to service: {}:{}", instance.getHost(), instance.getPort(), e));
    }

    public void removeConnection(ServiceInstance instance) {
        String key = getConnectionKey(instance);
        RSocketRequester requester = connections.remove(key);
        if (requester != null) {
            requester.rsocketClient().dispose();
        }
    }

    private String getConnectionKey(ServiceInstance instance) {
        return instance.getServiceName() + ":" + instance.getInstanceId();
    }
}
