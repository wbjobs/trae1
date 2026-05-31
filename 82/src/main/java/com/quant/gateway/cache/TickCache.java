package com.quant.gateway.cache;

import com.quant.gateway.codec.TickData;
import com.quant.gateway.config.GatewayConfig;

import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Iterator;
import java.util.List;
import java.util.ArrayList;

/**
 * 按股票代码缓存最近 N 笔 tick, 用于:
 *   1) 行情源断流时向前端提供最后一笔数据 (降级服务)
 *   2) 客户端重连时补发错过的 tick
 *
 * 线程安全: 读多写少, 使用 synchronized 保护; 写入速率 ~100k/s,
 *   单 symbol 平均 < 1k 写/s, 同步开销可忽略。
 */
public final class TickCache {

    private final Deque<TickData> deque;
    private final int capacity;
    private volatile TickData lastTick; // 最近一笔, 降级时快照用

    public TickCache() {
        this(GatewayConfig.CACHE_TICKS_PER_SYMBOL);
    }

    public TickCache(int capacity) {
        this.capacity = capacity;
        this.deque = new ArrayDeque<>(capacity + 1);
    }

    public synchronized void add(TickData tick) {
        if (deque.size() >= capacity) {
            deque.pollFirst();
        }
        deque.addLast(tick);
        lastTick = tick;
    }

    public synchronized TickData last() {
        return lastTick;
    }

    /**
     * 取最近 n 笔 (按时间升序), 不超过缓存深度。
     */
    public synchronized List<TickData> takeLast(int n) {
        int size = deque.size();
        int take = Math.min(n, size);
        List<TickData> out = new ArrayList<>(take);
        if (take == 0) return out;
        Iterator<TickData> it = deque.descendingIterator();
        for (int i = 0; i < take; i++) {
            out.add(it.next());
        }
        java.util.Collections.reverse(out);
        return out;
    }

    /**
     * 取 seq >= fromSeq 的最近 n 笔 (用于断线补发)。
     */
    public synchronized List<TickData> takeSince(long fromSeq, int max) {
        List<TickData> out = new ArrayList<>(max);
        for (TickData t : deque) {
            if (t.seq >= fromSeq) {
                out.add(t);
                if (out.size() >= max) break;
            }
        }
        return out;
    }

    public synchronized int size() {
        return deque.size();
    }
}
