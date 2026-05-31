package com.rsocket.gateway.connection;

import com.rsocket.gateway.config.ConnectionConfig;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.rsocket.RSocketRequester;
import org.springframework.stereotype.Component;

import java.time.Duration;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Component
public class ConnectionTracker {

    private final ConnectionConfig config;
    private final Map<String, ConnectionInfo> connections = new ConcurrentHashMap<>();

    public ConnectionTracker(ConnectionConfig config) {
        this.config = config;
    }

    public int getMaxPendingRequests() {
        return config.getMaxPendingRequests();
    }

    public long getIdleTimeoutMs() {
        return Duration.ofMinutes(config.getIdleTimeoutMinutes()).toMillis();
    }

    public long getKeepaliveIntervalMs() {
        return Duration.ofMinutes(config.getKeepaliveIntervalMinutes()).toMillis();
    }

    public ConnectionInfo registerConnection(String connectionId, String clientId,
                                              String serviceName, RSocketRequester requester) {
        ConnectionInfo info = ConnectionInfo.builder()
                .connectionId(connectionId)
                .clientId(clientId)
                .serviceName(serviceName)
                .requester(requester)
                .connectTime(Instant.now())
                .build();

        connections.put(connectionId, info);
        info.updateActivity();
        log.info("Connection registered: {} -> {}", clientId, serviceName);
        return info;
    }

    public void unregisterConnection(String connectionId) {
        ConnectionInfo info = connections.remove(connectionId);
        if (info != null) {
            info.setActive(false);
            log.info("Connection unregistered: {}, pending: {}, bufferedBytes: {}",
                    connectionId, info.getPendingRequests().get(), info.getTotalBufferedBytes().get());
        }
    }

    public ConnectionInfo getConnection(String connectionId) {
        return connections.get(connectionId);
    }

    public Collection<ConnectionInfo> getAllConnections() {
        return connections.values();
    }

    public boolean canAcceptRequest(String connectionId) {
        ConnectionInfo info = connections.get(connectionId);
        if (info == null || !info.isActive()) {
            return false;
        }
        return info.getPendingRequests().get() < getMaxPendingRequests();
    }

    public void incrementPending(String connectionId) {
        ConnectionInfo info = connections.get(connectionId);
        if (info != null) {
            int pending = info.incrementPending();
            int maxPending = getMaxPendingRequests();
            if (pending > maxPending * 0.8) {
                log.warn("High pending requests for {}: {}/{}", connectionId, pending, maxPending);
            }
        }
    }

    public void decrementPending(String connectionId) {
        ConnectionInfo info = connections.get(connectionId);
        if (info != null) {
            info.decrementPending();
        }
    }

    public void updateActivity(String connectionId) {
        ConnectionInfo info = connections.get(connectionId);
        if (info != null) {
            info.updateActivity();
        }
    }

    public List<ConnectionInfo> getIdleConnections() {
        List<ConnectionInfo> idle = new ArrayList<>();
        long idleTimeout = getIdleTimeoutMs();
        for (ConnectionInfo info : connections.values()) {
            if (info.isActive() && info.isIdle(idleTimeout)) {
                idle.add(info);
            }
        }
        return idle;
    }

    public List<ConnectionInfo> getConnectionsNeedingKeepalive() {
        List<ConnectionInfo> needKeepalive = new ArrayList<>();
        long keepaliveInterval = getKeepaliveIntervalMs();
        for (ConnectionInfo info : connections.values()) {
            if (info.isActive() && info.shouldSendKeepalive(keepaliveInterval)) {
                needKeepalive.add(info);
            }
        }
        return needKeepalive;
    }

    public ConnectionMemorySnapshot getMemorySnapshot() {
        long totalBufferedBytes = 0;
        int totalPending = 0;
        Map<String, ConnectionInfo> snapshot = new HashMap<>();

        for (Map.Entry<String, ConnectionInfo> entry : connections.entrySet()) {
            ConnectionInfo info = entry.getValue();
            totalBufferedBytes += info.getTotalBufferedBytes().get();
            totalPending += info.getPendingRequests().get();
            snapshot.put(entry.getKey(), info);
        }

        return ConnectionMemorySnapshot.builder()
                .totalConnections(connections.size())
                .totalPendingRequests(totalPending)
                .totalBufferedBytes(totalBufferedBytes)
                .timestamp(Instant.now())
                .connections(snapshot)
                .build();
    }

    @lombok.Data
    @lombok.Builder
    @lombok.NoArgsConstructor
    @lombok.AllArgsConstructor
    public static class ConnectionMemorySnapshot {
        private int totalConnections;
        private int totalPendingRequests;
        private long totalBufferedBytes;
        private Instant timestamp;
        private Map<String, ConnectionInfo> connections;
    }
}
