package com.quant.gateway.transport;

import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;
import com.quant.gateway.session.ClientSession;
import com.quant.gateway.session.NettyClientSession;
import com.quant.gateway.session.SubscriptionManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 行情分发器:
 *   - 对每笔 tick, 按 symbol / sector / kind 三个维度找出订阅者并推送;
 *   - 利用 ThreadLocal Set 去重 (一个客户端可能同时命中多个维度);
 *   - 背压传播: 当某个客户端进入背压状态, 暂停对其推送, 不影响其他客户端;
 *   - 全局降级: 行情源断流时, 停止推送增量 tick。
 *
 * 背压机制:
 *   - 每个 NettyClientSession 在写缓冲区超过高水位时调用 onPause()
 *   - 本类将该 clientId 加入 pausedClients 集合, dispatch() 中跳过
 *   - 当缓冲区恢复到低水位, 调用 onResume() 从集合中移除
 *   - 暂停期间 tick 不丢失: 由 NettyClientSession 自身的聚合队列兜底
 */
public final class DefaultFeedDispatcher implements FeedDispatcher, NettyClientSession.BackPressureListener {

    private static final Logger log = LoggerFactory.getLogger(DefaultFeedDispatcher.class);

    private final SubscriptionManager subscriptions;
    private final MarketCache cache;

    private volatile boolean degraded = false;

    /** 背压暂停的 clientId 集合 */
    private final Set<String> pausedClients = ConcurrentHashMap.newKeySet();

    /** 统计各客户端被跳过的 tick 数 (仅用于观测) */
    private final ConcurrentHashMap<String, Long> skippedTicks = new ConcurrentHashMap<>();

    private final ThreadLocal<Set<ClientSession>> dedupe =
            ThreadLocal.withInitial(() -> ConcurrentHashMap.newKeySet());

    public DefaultFeedDispatcher(SubscriptionManager subscriptions, MarketCache cache) {
        this.subscriptions = subscriptions;
        this.cache = cache;
    }

    @Override
    public void dispatch(TickData tick) {
        if (degraded) return;
        Set<ClientSession> target = dedupe.get();
        target.clear();
        target.addAll(subscriptions.findSymbol(tick.symbol));
        target.addAll(subscriptions.findSector(tick.sector));
        target.addAll(subscriptions.findKind(tick.msgType));
        if (target.isEmpty()) return;
        for (ClientSession s : target) {
            if (!s.isActive()) continue;
            if (pausedClients.contains(s.getClientId())) {
                skippedTicks.merge(s.getClientId(), 1L, Long::sum);
                continue;
            }
            s.pushTick(tick);
        }
    }

    // ========== 背压传播 (BackPressureListener) ==========

    @Override
    public void onPause(NettyClientSession session) {
        String clientId = session.getClientId();
        if (pausedClients.add(clientId)) {
            log.warn("[{}] upstream dispatch paused due to back pressure (pendingBytes={})",
                    clientId, session.pendingBytes());
        }
    }

    @Override
    public void onResume(NettyClientSession session) {
        String clientId = session.getClientId();
        if (pausedClients.remove(clientId)) {
            Long skipped = skippedTicks.remove(clientId);
            log.info("[{}] upstream dispatch resumed (skippedTicks={}, duration={}ms)",
                    clientId, skipped == null ? 0 : skipped, session.backPressureDurationMs());
        }
    }

    // ========== 全局降级 ==========

    @Override
    public void onFeedDown() {
        if (degraded) return;
        degraded = true;
        cache.setDegraded(true);
        log.warn("FEED DOWN -> enter degraded mode");
    }

    @Override
    public void onFeedUp() {
        if (!degraded) return;
        degraded = false;
        cache.setDegraded(false);
        log.info("FEED UP -> resume normal");
    }

    // ========== 补发 / 控制 ==========

    @Override
    public void replay(ClientSession session, List<TickData> ticks) {
        if (!session.isActive()) return;
        session.pushReplay(ticks);
    }

    @Override
    public void pushControl(ClientSession session, MsgType type, byte[] payload) {
        session.pushControl(type, payload);
    }

    // ========== 状态查询 ==========

    public boolean isDegraded() { return degraded; }

    public boolean isPaused(String clientId) { return pausedClients.contains(clientId); }

    public int pausedCount() { return pausedClients.size(); }

    public Set<String> pausedClientIds() { return pausedClients; }

    public long skippedCount(String clientId) {
        Long v = skippedTicks.get(clientId);
        return v == null ? 0 : v;
    }
}
