package com.quant.gateway.session;

import com.quant.gateway.config.GatewayConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.*;

/**
 * 会话注册表: 同时维护 "在线会话" 和 "离线会话缓存"。
 *
 *  - 在线会话: clientId -> ClientSession (最多 MAX_CLIENTS)
 *  - 离线会话缓存: 最近 SESSION_TTL 内断开的 clientId -> 最后 ackSeq,
 *    用于客户端断线重连时补发错过的 tick。
 */
public final class SessionRegistry {

    private static final Logger log = LoggerFactory.getLogger(SessionRegistry.class);

    private final ConcurrentHashMap<String, ClientSession> online = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Long> offlineAckSeq = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Long> offlineTs = new ConcurrentHashMap<>();

    public SessionRegistry(ScheduledExecutorService scheduler) {
        scheduler.scheduleAtFixedRate(this::evictExpired, 60, 60, TimeUnit.SECONDS);
    }

    public ClientSession online(String clientId, ClientSession session) {
        if (online.size() >= GatewayConfig.MAX_CLIENTS) {
            return null;
        }
        ClientSession prev = online.putIfAbsent(clientId, session);
        if (prev != null) {
            prev.close();
            online.put(clientId, session);
        }
        return session;
    }

    public void offline(String clientId, long ackSeq) {
        ClientSession s = online.remove(clientId);
        if (s != null) {
            offlineAckSeq.put(clientId, ackSeq);
            offlineTs.put(clientId, System.currentTimeMillis());
            log.info("session offline: {} (lastSeq={})", clientId, ackSeq);
        }
    }

    public ClientSession findOnline(String clientId) { return online.get(clientId); }

    public long offlineAckSeq(String clientId) {
        Long v = offlineAckSeq.get(clientId);
        return v == null ? 0 : v;
    }

    public void clearOffline(String clientId) {
        offlineAckSeq.remove(clientId);
        offlineTs.remove(clientId);
    }

    public int onlineCount() { return online.size(); }
    public int offlineCount() { return offlineAckSeq.size(); }

    public ConcurrentHashMap<String, ClientSession> allOnline() { return online; }
    public ConcurrentHashMap<String, Long> allOfflineTs() { return offlineTs; }

    private void evictExpired() {
        long now = System.currentTimeMillis();
        long ttl = GatewayConfig.CLIENT_SESSION_TTL_SEC * 1000L;
        offlineTs.entrySet().removeIf(e -> {
            if (now - e.getValue() > ttl) {
                offlineAckSeq.remove(e.getKey());
                log.debug("evict offline session: {}", e.getKey());
                return true;
            }
            return false;
        });
    }
}
