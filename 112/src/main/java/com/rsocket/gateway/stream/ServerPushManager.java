package com.rsocket.gateway.stream;

import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Component;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Sinks;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Component
public class ServerPushManager {

    private final Map<String, Sinks.Many<Object>> pushSinks = new ConcurrentHashMap<>();

    public Flux<Object> subscribeToPush(String clientId) {
        Sinks.Many<Object> sink = pushSinks.computeIfAbsent(clientId,
                k -> Sinks.many().multicast().onBackpressureBuffer());
        return sink.asFlux()
                .doOnCancel(() -> log.info("Client {} unsubscribed from push", clientId));
    }

    public void pushToClient(String clientId, Object message) {
        Sinks.Many<Object> sink = pushSinks.get(clientId);
        if (sink != null) {
            sink.tryEmitNext(message);
            log.debug("Pushed message to client: {}", clientId);
        } else {
            log.warn("No sink found for client: {}", clientId);
        }
    }

    public void broadcast(Object message) {
        pushSinks.forEach((clientId, sink) -> {
            try {
                sink.tryEmitNext(message);
            } catch (Exception e) {
                log.error("Failed to broadcast to client: {}", clientId, e);
            }
        });
        log.info("Broadcasted message to {} clients", pushSinks.size());
    }

    public void removeClient(String clientId) {
        Sinks.Many<Object> sink = pushSinks.remove(clientId);
        if (sink != null) {
            sink.tryEmitComplete();
        }
    }
}
