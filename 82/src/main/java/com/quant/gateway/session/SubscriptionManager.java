package com.quant.gateway.session;

import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 订阅管理器:
 *   - 维护 symbol->订阅会话集合
 *   - 维护 sector->订阅会话集合
 *   - 维护 kind(TICK/ORDER) -> 订阅会话集合
 *   - 维护 clientId->订阅视图 (用于 REST API 查询)
 */
public final class SubscriptionManager {

    public enum Scope { SYMBOL, SECTOR, KIND }

    public static final class SubView {
        public final Set<String> symbols = ConcurrentHashMap.newKeySet();
        public final Set<String> sectors = ConcurrentHashMap.newKeySet();
        public final Set<MsgType> kinds   = EnumSet.noneOf(MsgType.class);
    }

    private final ConcurrentHashMap<String, Set<ClientSession>> bySymbol = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Set<ClientSession>> bySector = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<MsgType, Set<ClientSession>> byKind = new ConcurrentHashMap<>();

    private final ConcurrentHashMap<String, SubView> clientView = new ConcurrentHashMap<>();

    // ========== 查询 ==========

    public List<ClientSession> findSymbol(String symbol) {
        Set<ClientSession> s = bySymbol.get(symbol);
        return s == null ? Collections.emptyList() : new ArrayList<>(s);
    }

    public List<ClientSession> findSector(String sector) {
        Set<ClientSession> s = bySector.get(sector);
        return s == null ? Collections.emptyList() : new ArrayList<>(s);
    }

    public List<ClientSession> findKind(MsgType kind) {
        Set<ClientSession> s = byKind.get(kind);
        return s == null ? Collections.emptyList() : new ArrayList<>(s);
    }

    public SubView viewOf(String clientId) { return clientView.get(clientId); }

    public Map<String, SubView> allViews() { return clientView; }

    // ========== 订阅/取消 ==========

    public void subscribeSymbol(String clientId, ClientSession session, String symbol) {
        bySymbol.computeIfAbsent(symbol, k -> ConcurrentHashMap.newKeySet()).add(session);
        view(clientId).symbols.add(symbol);
    }

    public void subscribeSector(String clientId, ClientSession session, String sector) {
        bySector.computeIfAbsent(sector, k -> ConcurrentHashMap.newKeySet()).add(session);
        view(clientId).sectors.add(sector);
    }

    public void subscribeKind(String clientId, ClientSession session, MsgType kind) {
        byKind.computeIfAbsent(kind, k -> ConcurrentHashMap.newKeySet()).add(session);
        view(clientId).kinds.add(kind);
    }

    public void unsubscribeSymbol(String clientId, ClientSession session, String symbol) {
        Set<ClientSession> s = bySymbol.get(symbol);
        if (s != null) { s.remove(session); if (s.isEmpty()) bySymbol.remove(symbol); }
        SubView v = clientView.get(clientId);
        if (v != null) v.symbols.remove(symbol);
    }

    public void unsubscribeSector(String clientId, ClientSession session, String sector) {
        Set<ClientSession> s = bySector.get(sector);
        if (s != null) { s.remove(session); if (s.isEmpty()) bySector.remove(sector); }
        SubView v = clientView.get(clientId);
        if (v != null) v.sectors.remove(sector);
    }

    public void unsubscribeKind(String clientId, ClientSession session, MsgType kind) {
        Set<ClientSession> s = byKind.get(kind);
        if (s != null) { s.remove(session); if (s.isEmpty()) byKind.remove(kind); }
        SubView v = clientView.get(clientId);
        if (v != null) v.kinds.remove(kind);
    }

    /**
     * 清理某个会话的所有订阅 (会话断开时调用)。
     */
    public void clearSession(ClientSession session) {
        SubView v = clientView.get(session.getClientId());
        if (v == null) return;
        for (String sym : v.symbols) {
            Set<ClientSession> s = bySymbol.get(sym);
            if (s != null) { s.remove(session); if (s.isEmpty()) bySymbol.remove(sym); }
        }
        for (String sec : v.sectors) {
            Set<ClientSession> s = bySector.get(sec);
            if (s != null) { s.remove(session); if (s.isEmpty()) bySector.remove(sec); }
        }
        for (MsgType k : v.kinds) {
            Set<ClientSession> s = byKind.get(k);
            if (s != null) { s.remove(session); if (s.isEmpty()) byKind.remove(k); }
        }
        clientView.remove(session.getClientId());
    }

    public int totalSubscribers() {
        AtomicInteger cnt = new AtomicInteger();
        bySymbol.values().forEach(s -> cnt.addAndGet(s.size()));
        return cnt.get();
    }

    private SubView view(String clientId) {
        return clientView.computeIfAbsent(clientId, k -> new SubView());
    }
}
