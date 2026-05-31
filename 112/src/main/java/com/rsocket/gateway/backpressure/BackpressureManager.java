package com.rsocket.gateway.backpressure;

import com.rsocket.gateway.connection.ConnectionInfo;
import com.rsocket.gateway.connection.ConnectionTracker;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Component;
import reactor.core.publisher.Flux;
import reactor.core.publisher.Mono;

import java.util.function.Consumer;

@Slf4j
@Component
@RequiredArgsConstructor
public class BackpressureManager {

    private final ConnectionTracker connectionTracker;

    public <T> Flux<T> propagateBackpressure(String connectionId, Flux<T> upstream,
                                             Consumer<Integer> requestNForwarder) {
        ConnectionInfo info = connectionTracker.getConnection(connectionId);
        if (info == null) {
            return upstream;
        }

        return upstream
                .doOnRequest(requested -> {
                    log.debug("Client requested {} items on connection {}", requested, connectionId);
                    requestNForwarder.accept((int) requested);
                })
                .doOnNext(item -> {
                    info.updateActivity();
                    info.decrementPending();
                })
                .doOnComplete(() -> {
                    log.debug("Stream completed for connection {}", connectionId);
                    info.updateActivity();
                })
                .doOnError(error -> {
                    log.warn("Stream error for connection {}: {}", connectionId, error.getMessage());
                    info.updateActivity();
                })
                .doOnSubscribe(subscription -> {
                    log.debug("Backpressure propagated for connection {}", connectionId);
                })
                .onBackpressureBuffer(100, buffer -> {
                    log.warn("Buffer overflow for connection {}, dropping {} items",
                            connectionId, buffer.size());
                    info.getBufferCount().addAndGet(-buffer.size());
                });
    }

    public <T> Mono<T> propagateBackpressureMono(String connectionId, Mono<T> upstream) {
        ConnectionInfo info = connectionTracker.getConnection(connectionId);
        if (info == null) {
            return upstream;
        }

        return upstream
                .doOnSuccess(item -> {
                    info.updateActivity();
                    info.decrementPending();
                })
                .doOnError(error -> {
                    info.updateActivity();
                    log.warn("Request error for connection {}: {}", connectionId, error.getMessage());
                });
    }

    public boolean checkAndAcceptRequest(String connectionId) {
        if (!connectionTracker.canAcceptRequest(connectionId)) {
            ConnectionInfo info = connectionTracker.getConnection(connectionId);
            log.warn("Request rejected for connection {}, pending: {}, max: {}",
                    connectionId,
                    info != null ? info.getPendingRequests().get() : "unknown",
                    connectionTracker.getMaxPendingRequests());
            return false;
        }
        connectionTracker.incrementPending(connectionId);
        return true;
    }

    public void forwardRequestN(RSocketRequester backendRequester, int n) {
        log.debug("Forwarding Request N({}) to backend", n);
    }
}
