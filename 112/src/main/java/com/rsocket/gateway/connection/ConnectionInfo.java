package com.rsocket.gateway.connection;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;
import org.springframework.messaging.rsocket.RSocketRequester;

import java.time.Instant;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ConnectionInfo {
    private String connectionId;
    private String clientId;
    private String serviceName;
    private RSocketRequester requester;
    private final AtomicInteger pendingRequests = new AtomicInteger(0);
    private final AtomicLong totalBufferedBytes = new AtomicLong(0);
    private final AtomicInteger bufferCount = new AtomicInteger(0);
    private final AtomicLong lastActivityTime = new AtomicLong(System.currentTimeMillis());
    private final AtomicLong keepaliveLastSent = new AtomicLong(System.currentTimeMillis());
    private volatile boolean active = true;
    private Instant connectTime;

    public int incrementPending() {
        return pendingRequests.incrementAndGet();
    }

    public int decrementPending() {
        return pendingRequests.decrementAndGet();
    }

    public void addBufferedBytes(long bytes) {
        totalBufferedBytes.addAndGet(bytes);
        bufferCount.incrementAndGet();
    }

    public void removeBufferedBytes(long bytes) {
        totalBufferedBytes.addAndGet(-bytes);
        bufferCount.decrementAndGet();
    }

    public void updateActivity() {
        lastActivityTime.set(System.currentTimeMillis());
    }

    public boolean isIdle(long idleTimeoutMs) {
        return System.currentTimeMillis() - lastActivityTime.get() > idleTimeoutMs;
    }

    public boolean shouldSendKeepalive(long keepaliveIntervalMs) {
        return System.currentTimeMillis() - keepaliveLastSent.get() > keepaliveIntervalMs;
    }

    public void markKeepaliveSent() {
        keepaliveLastSent.set(System.currentTimeMillis());
    }
}
