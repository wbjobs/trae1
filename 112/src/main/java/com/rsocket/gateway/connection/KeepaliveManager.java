package com.rsocket.gateway.connection;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Slf4j
@Component
@RequiredArgsConstructor
public class KeepaliveManager {

    private final ConnectionTracker connectionTracker;

    @Scheduled(fixedRate = 60000)
    public void detectIdleConnections() {
        var idleConnections = connectionTracker.getIdleConnections();
        for (ConnectionInfo info : idleConnections) {
            log.warn("Idle connection detected: {}, client: {}, service: {}, idle: {}ms",
                    info.getConnectionId(), info.getClientId(),
                    info.getServiceName(),
                    System.currentTimeMillis() - info.getLastActivityTime().get());

            if (System.currentTimeMillis() - info.getLastActivityTime().get()
                    > connectionTracker.getIdleTimeoutMs() * 2) {
                disconnectConnection(info, "Idle timeout exceeded");
            }
        }
    }

    @Scheduled(fixedRate = 300000)
    public void sendKeepalives() {
        var connections = connectionTracker.getConnectionsNeedingKeepalive();
        for (ConnectionInfo info : connections) {
            sendKeepalive(info);
        }
    }

    @Scheduled(fixedRate = 30000)
    public void detectOverloadedConnections() {
        int maxPending = connectionTracker.getMaxPendingRequests();
        for (ConnectionInfo info : connectionTracker.getAllConnections()) {
            int pending = info.getPendingRequests().get();
            if (pending > maxPending) {
                log.warn("Connection {} has {} pending requests, exceeding limit {}",
                        info.getConnectionId(), pending, maxPending);
            }
        }
    }

    private void sendKeepalive(ConnectionInfo info) {
        try {
            log.debug("Sending keepalive to connection: {}", info.getConnectionId());

            if (info.getRequester() != null) {
                info.getRequester().route("keepalive")
                        .data("ping")
                        .retrieveMono(String.class)
                        .doOnSuccess(response -> {
                            log.debug("Keepalive response received from {}", info.getConnectionId());
                            info.markKeepaliveSent();
                            info.updateActivity();
                        })
                        .doOnError(error -> {
                            log.warn("Keepalive failed for {}: {}", info.getConnectionId(), error.getMessage());
                            handleKeepaliveFailure(info);
                        })
                        .subscribe();
            } else {
                handleKeepaliveFailure(info);
            }
        } catch (Exception e) {
            log.error("Error sending keepalive to {}: {}", info.getConnectionId(), e.getMessage());
            handleKeepaliveFailure(info);
        }
    }

    private void handleKeepaliveFailure(ConnectionInfo info) {
        if (!info.isActive()) {
            return;
        }

        long lastActivity = System.currentTimeMillis() - info.getLastActivityTime().get();
        if (lastActivity > connectionTracker.getIdleTimeoutMs()) {
            disconnectConnection(info, "Keepalive failure and idle timeout");
        }
    }

    public void disconnectConnection(ConnectionInfo info, String reason) {
        if (!info.isActive()) {
            return;
        }

        log.info("Disconnecting connection {}: {}", info.getConnectionId(), reason);

        try {
            if (info.getRequester() != null) {
                info.getRequester().rsocketClient().dispose();
            }
        } catch (Exception e) {
            log.error("Error disposing connection {}: {}", info.getConnectionId(), e.getMessage());
        }

        connectionTracker.unregisterConnection(info.getConnectionId());
    }

    public void disconnectConnection(String connectionId, String reason) {
        ConnectionInfo info = connectionTracker.getConnection(connectionId);
        if (info != null) {
            disconnectConnection(info, reason);
        }
    }
}
