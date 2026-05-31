package com.quant.gateway.cache;

import com.quant.gateway.codec.TickData;
import com.quant.gateway.config.GatewayConfig;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 全局行情缓存:
 *   - 按 symbol 维度缓存 (TickCache per symbol)
 *   - 按 sector 维度聚合索引 (便于按板块补发)
 *   - 全局流水号 seq 用于客户端断线补发定位
 *   - 最近一笔数据用于断流降级
 */
public final class MarketCache {

    private final ConcurrentHashMap<String, TickCache> bySymbol = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Set<String>> sectorMembers = new ConcurrentHashMap<>();
    private final AtomicLong seqGenerator = new AtomicLong(0);

    // 行情源健康状态: 最近一次收到数据的时间
    private volatile long lastFeedTs = System.currentTimeMillis();
    private volatile boolean degraded = false;

    public long nextSeq() {
        return seqGenerator.incrementAndGet();
    }

    public void markFeed() {
        lastFeedTs = System.currentTimeMillis();
        degraded = false;
    }

    public boolean isFeedAlive() {
        return (System.currentTimeMillis() - lastFeedTs) < GatewayConfig.FEED_TIMEOUT_SEC * 1000L;
    }

    public void setDegraded(boolean v) { this.degraded = v; }
    public boolean isDegraded() { return degraded; }

    public void put(TickData tick) {
        bySymbol.computeIfAbsent(tick.symbol, k -> new TickCache()).add(tick);
        sectorMembers.computeIfAbsent(tick.sector, k -> ConcurrentHashMap.newKeySet()).add(tick.symbol);
        markFeed();
    }

    public TickData last(String symbol) {
        TickCache c = bySymbol.get(symbol);
        return c == null ? null : c.last();
    }

    public List<TickData> replayBySymbol(String symbol, long fromSeq) {
        TickCache c = bySymbol.get(symbol);
        if (c == null) return Collections.emptyList();
        return c.takeSince(fromSeq, GatewayConfig.REPLAY_TICKS_MAX);
    }

    public List<TickData> replayBySector(String sector, long fromSeq) {
        Set<String> members = sectorMembers.get(sector);
        if (members == null) return Collections.emptyList();
        List<TickData> out = new ArrayList<>();
        for (String s : members) {
            TickCache c = bySymbol.get(s);
            if (c == null) continue;
            out.addAll(c.takeSince(fromSeq, GatewayConfig.REPLAY_TICKS_MAX));
        }
        out.sort(Comparator.comparingLong(t -> t.seq));
        if (out.size() > GatewayConfig.REPLAY_TICKS_MAX) {
            out = out.subList(out.size() - GatewayConfig.REPLAY_TICKS_MAX, out.size());
        }
        return out;
    }

    public List<TickData> snapshotLast(List<String> symbols) {
        List<TickData> out = new ArrayList<>(symbols.size());
        for (String s : symbols) {
            TickData t = last(s);
            if (t != null) out.add(t);
        }
        return out;
    }

    public Map<String, Integer> cacheStats() {
        Map<String, Integer> m = new HashMap<>();
        for (Map.Entry<String, TickCache> e : bySymbol.entrySet()) {
            m.put(e.getKey(), e.getValue().size());
        }
        return m;
    }
}
