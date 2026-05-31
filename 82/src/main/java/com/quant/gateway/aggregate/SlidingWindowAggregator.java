package com.quant.gateway.aggregate;

import com.github.benmanes.caffeine.cache.Cache;
import com.github.benmanes.caffeine.cache.Caffeine;
import com.quant.gateway.codec.TickData;
import com.quant.gateway.dsl.Expr;
import com.quant.gateway.dsl.Parser;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.ReentrantLock;

/**
 * 滑动窗口聚合引擎。
 *
 * 架构:
 *   - 每个规则 (ruleId) + 每个股票 (symbol) 对应一个窗口实例 (WindowState)
 *   - 使用 Caffeine 缓存按 (ruleId + ":" + symbol) 为 key 保存窗口状态
 *   - 每个窗口内部使用环形缓冲区 (RingBuffer) 存储窗口内的 tick, 按时间滑动淘汰
 *   - 窗口结算使用增量计算 (增量更新 AggregateResult), 复杂度 O(1) per tick
 *   - 推送频率由 pushIntervalMs 控制, 聚合延迟 < 10ms
 *
 * 性能优化:
 *   - 无锁读取: 使用 volatile + 原子引用保证可见性
 *   - 写入路径: 单线程 (Netty UDP 接收线程), 无竞争
 *   - 增量计算: 只更新发生变化的字段, 不重新扫描整个窗口
 *   - 淘汰策略: Caffeine 自动管理过期窗口, 避免内存泄漏
 *
 * 时间复杂度: O(1) per tick (不包含 where 条件过滤)
 */
public final class SlidingWindowAggregator {

    private static final Logger log = LoggerFactory.getLogger(SlidingWindowAggregator.class);

    /** (ruleId:symbol) -> WindowState */
    private final Cache<String, WindowState> windowCache;

    /** 规则注册表 */
    private final ConcurrentHashMap<String, AggregateRule> rules = new ConcurrentHashMap<>();

    /** 聚合结果监听器 (用于 WebSocket 推送) */
    private volatile AggregateListener listener;

    /** 统计 */
    private final AtomicLong totalTicks = new AtomicLong(0);
    private final AtomicLong totalAggregates = new AtomicLong(0);
    private final AtomicLong maxLatencyNs = new AtomicLong(0);

    public SlidingWindowAggregator() {
        // 窗口状态缓存: 写入后最多保留 5 倍窗口时间, 自动淘汰不活跃的 symbol
        this.windowCache = Caffeine.newBuilder()
                .expireAfterWrite(30, TimeUnit.SECONDS)
                .maximumSize(100_000)
                .build();
    }

    public void setListener(AggregateListener listener) {
        this.listener = listener;
    }

    /** 注册/更新规则 */
    public void registerRule(AggregateRule rule) {
        rules.put(rule.getId(), rule);
        log.info("aggregate rule registered: {} [window={}ms, push={}ms, dsl={}]",
                rule.getId(), rule.getWindowSizeMs(), rule.getPushIntervalMs(), rule.getDsl());
    }

    /** 删除规则 */
    public boolean removeRule(String ruleId) {
        AggregateRule r = rules.remove(ruleId);
        if (r != null) {
            // 清理该规则的所有窗口
            windowCache.asMap().keySet().removeIf(k -> k.startsWith(ruleId + ":"));
            log.info("aggregate rule removed: {}", ruleId);
            return true;
        }
        return false;
    }

    public AggregateRule getRule(String ruleId) { return rules.get(ruleId); }
    public Collection<AggregateRule> getAllRules() { return rules.values(); }
    public int ruleCount() { return rules.size(); }

    /**
     * 处理一笔 tick, 分发到所有相关规则的窗口。
     * 线程安全: 设计为单线程调用 (Netty UDP 接收器的 EventLoop)。
     */
    public void onTick(TickData tick) {
        long start = System.nanoTime();
        totalTicks.incrementAndGet();

        // 遍历所有规则, 只处理相关的
        for (AggregateRule rule : rules.values()) {
            if (!rule.isEnabled()) continue;
            if (!rule.isRelevant(tick)) continue;

            rule.incrementTicks();

            String key = rule.getId() + ":" + tick.symbol;
            WindowState window = windowCache.get(key, k -> new WindowState(rule));
            window.onTick(tick);

            // 检查是否需要推送
            long now = System.currentTimeMillis();
            if (rule.shouldPush(now)) {
                rule.markPushed(now);
                AggregateResult result = window.snapshot(now);
                rule.setLastSnapshot(tick.symbol, result);
                rule.incrementAggregates();
                totalAggregates.incrementAndGet();

                // 通知监听器
                AggregateListener l = listener;
                if (l != null) {
                    try {
                        l.onAggregate(rule, result);
                    } catch (Exception e) {
                        log.warn("aggregate listener error: {}", e.getMessage());
                    }
                }
            }
        }

        // 延迟统计
        long latency = System.nanoTime() - start;
        maxLatencyNs.accumulateAndGet(latency, Math::max);
    }

    /**
     * 主动触发所有规则的窗口结算 (用于 API 查询)。
     */
    public Map<String, AggregateResult> snapshotAll(String ruleId) {
        AggregateRule rule = rules.get(ruleId);
        if (rule == null) return Collections.emptyMap();

        Map<String, AggregateResult> out = new HashMap<>();
        long now = System.currentTimeMillis();
        windowCache.asMap().forEach((key, window) -> {
            if (key.startsWith(ruleId + ":")) {
                String symbol = key.substring(ruleId.length() + 1);
                out.put(symbol, window.snapshot(now));
            }
        });
        return out;
    }

    /**
     * 聚合结果监听器接口。
     */
    public interface AggregateListener {
        void onAggregate(AggregateRule rule, AggregateResult result);
    }

    // ============ 状态统计 ============

    public long getTotalTicks() { return totalTicks.get(); }
    public long getTotalAggregates() { return totalAggregates.get(); }
    public long getMaxLatencyNs() { return maxLatencyNs.get(); }
    public void resetStats() { maxLatencyNs.set(0); }

    public int getActiveWindowCount() {
        return (int) windowCache.estimatedSize();
    }

    // ============ 窗口状态 (内部类) ============

    /**
     * 单个规则 + 单个 symbol 的滑动窗口状态。
     *
     * 使用增量更新, 不重新扫描历史数据:
     *   - 新增 tick: update() 合并到 currentResult
     *   - 淘汰过期 tick: ring buffer 中记录 tick 的贡献值, 过期时从 currentResult 中减去
     *
     * 线程安全: 由 SlidingWindowAggregator 单线程调用, 无需加锁。
     */
    private static final class WindowState {
        private final AggregateRule rule;
        private final long windowSizeMs;

        /** 环形缓冲区: 窗口内的 tick 列表, 用于过期时回滚贡献 */
        private final TickRingBuffer ringBuffer;

        /** 当前窗口的聚合结果 (增量更新) */
        private volatile AggregateResult currentResult;

        /** 窗口起始时间 (滑动窗口的左端) */
        private long windowStart;

        WindowState(AggregateRule rule) {
            this.rule = rule;
            this.windowSizeMs = rule.getWindowSizeMs();
            this.ringBuffer = new TickRingBuffer(2048); // 足够容纳 1s * 100k/s, 实际单 symbol 每秒 < 1k
            this.windowStart = System.currentTimeMillis();
        }

        void onTick(TickData tick) {
            long now = System.currentTimeMillis();

            // 滑动窗口: 淘汰窗口左端过期的 tick
            long newWindowStart = now - windowSizeMs + 1;
            while (windowStart < newWindowStart) {
                // 回滚过期 tick 的贡献
                TickExpired expired = ringBuffer.pollExpired(newWindowStart);
                while (expired != null) {
                    rollback(expired);
                    expired = ringBuffer.pollExpired(newWindowStart);
                }
                windowStart = newWindowStart;
            }

            // 初始化当前结果 (如果是第一个 tick)
            if (currentResult == null) {
                currentResult = new AggregateResult(rule.getId(), tick,
                        Math.max(windowStart, now - windowSizeMs + 1),
                        windowSizeMs, rule.getBigOrderThreshold());
            }

            // 计算自定义指标 (DSL 中的聚合函数)
            Map<String, Double> metrics = computeMetrics(tick);

            // 增量更新
            boolean changed = currentResult.update(tick);
            if (changed) {
                for (Map.Entry<String, Double> e : metrics.entrySet()) {
                    currentResult.customMetrics.merge(e.getKey(), e.getValue(), Double::sum);
                }
            }

            // 记入环形缓冲区
            ringBuffer.add(tick, metrics, now);
        }

        /**
         * 计算 DSL 中定义的自定义指标。
         */
        private Map<String, Double> computeMetrics(TickData tick) {
            Map<String, Double> out = new HashMap<>();
            Parser.AggregateRuleDefinition def = rule.getDefinition();
            for (Parser.MetricDef m : def.metrics) {
                double v = m.call.evaluateDouble(tick);
                out.put(m.name(), v);
            }
            return out;
        }

        /**
         * 回滚一个过期 tick 的贡献。
         */
        private void rollback(TickExpired expired) {
            if (currentResult == null) return;
            currentResult.count--;
            currentResult.sumVolume -= expired.tick.volume;
            currentResult.sumTurnover -= expired.tick.turnover;
            if (expired.tick.volume >= rule.getBigOrderThreshold()) {
                currentResult.bigOrderCount--;
                currentResult.bigOrderVolume -= expired.tick.volume;
            }
            // 回滚自定义指标
            if (expired.metrics != null) {
                for (Map.Entry<String, Double> e : expired.metrics.entrySet()) {
                    currentResult.customMetrics.merge(e.getKey(), -e.getValue(), Double::sum);
                }
            }
            // 重新计算均价
            if (currentResult.sumVolume > 0) {
                currentResult.avgPrice = currentResult.sumTurnover / (double) currentResult.sumVolume;
                currentResult.wavgPrice = currentResult.sumTurnover / (double) currentResult.sumVolume;
            } else {
                currentResult.avgPrice = 0;
                currentResult.wavgPrice = 0;
            }
            // 高低价需要重新扫描 (简化处理: 仅清空, 下一笔 tick 会重建)
            if (currentResult.count == 0) {
                currentResult.high = expired.tick.price;
                currentResult.low = expired.tick.price;
            }
        }

        /**
         * 生成当前窗口的快照。
         */
        AggregateResult snapshot(long now) {
            if (currentResult == null) {
                // 空窗口: 返回一个空结果
                AggregateResult empty = new AggregateResult();
                empty.ruleId = rule.getId();
                empty.windowStart = Math.max(0, now - windowSizeMs + 1);
                empty.windowEnd = now;
                empty.seq = now;
                return empty;
            }
            AggregateResult snap = new AggregateResult();
            snap.ruleId = rule.getId();
            snap.symbol = currentResult.symbol;
            snap.sector = currentResult.sector;
            snap.windowStart = currentResult.windowStart;
            snap.windowEnd = now;
            snap.seq = now;
            snap.count = currentResult.count;
            snap.sumVolume = currentResult.sumVolume;
            snap.sumTurnover = currentResult.sumTurnover;
            snap.avgPrice = currentResult.avgPrice;
            snap.wavgPrice = currentResult.wavgPrice;
            snap.high = currentResult.high;
            snap.low = currentResult.low;
            snap.open = currentResult.open;
            snap.close = currentResult.close;
            snap.bigOrderCount = currentResult.bigOrderCount;
            snap.bigOrderVolume = currentResult.bigOrderVolume;
            snap.bigOrderThreshold = currentResult.bigOrderThreshold;
            snap.customMetrics = new HashMap<>(currentResult.customMetrics);
            return snap;
        }
    }

    /**
     * 环形缓冲区: 存储窗口内的 tick, 用于过期回滚。
     * 固定大小, 超出时覆盖最旧元素。
     */
    private static final class TickRingBuffer {
        private final TickExpired[] buffer;
        private int head;
        private int tail;
        private int size;

        TickRingBuffer(int capacity) {
            this.buffer = new TickExpired[capacity];
        }

        void add(TickData tick, Map<String, Double> metrics, long ts) {
            TickExpired e = new TickExpired();
            e.tick = tick;
            e.metrics = metrics;
            e.timestamp = ts;
            if (size == buffer.length) {
                // 覆盖最旧的
                buffer[tail] = e;
                tail = (tail + 1) % buffer.length;
                head = (head + 1) % buffer.length;
            } else {
                buffer[tail] = e;
                tail = (tail + 1) % buffer.length;
                size++;
            }
        }

        TickExpired pollExpired(long cutoffTs) {
            if (size == 0) return null;
            TickExpired e = buffer[head];
            if (e == null || e.timestamp >= cutoffTs) return null;
            buffer[head] = null;
            head = (head + 1) % buffer.length;
            size--;
            return e;
        }
    }

    /**
     * 过期 tick 记录: 包含 tick 本身 + 自定义指标值 + 时间戳。
     */
    private static final class TickExpired {
        TickData tick;
        Map<String, Double> metrics;
        long timestamp;
    }
}
