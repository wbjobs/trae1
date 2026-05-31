package com.quant.gateway.aggregate;

import com.quant.gateway.codec.TickData;
import com.quant.gateway.dsl.Expr;
import com.quant.gateway.dsl.Lexer;
import com.quant.gateway.dsl.Parser;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 聚合规则：管理单个规则的完整生命周期。
 *
 * 包含:
 *   - 规则元数据 (id, name, dsl, windowSizeMs, 股票范围)
 *   - 编译后的 DSL 表达式 (filter + metrics)
 *   - 滑动窗口状态 (使用 Caffeine 缓存按 symbol 维度保存窗口数据)
 *   - 最近一次推送的快照 (用于 Delta 编码)
 */
public final class AggregateRule {

    private final String id;
    private final String name;
    private final String dsl;
    private final long windowSizeMs;
    private final Set<String> symbols;  // 空集表示所有股票
    private final Set<String> sectors;  // 空集表示所有板块
    private final long bigOrderThreshold;
    private final long pushIntervalMs;
    private final boolean deltaEnabled;

    /** 编译后的 DSL */
    private final Parser.AggregateRuleDefinition definition;

    /** 最后一次推送的快照 (每个 symbol 一个), 用于 Delta 编码 */
    private final ConcurrentHashMap<String, AggregateResult> lastSnapshot = new ConcurrentHashMap<>();

    /** 规则状态 */
    private volatile boolean enabled = true;
    private volatile long lastPushTs = 0;
    private final AtomicLong totalTicksProcessed = new AtomicLong(0);
    private final AtomicLong totalAggregatesGenerated = new AtomicLong(0);

    private AggregateRule(Builder b) {
        this.id = b.id;
        this.name = b.name;
        this.dsl = b.dsl;
        this.windowSizeMs = b.windowSizeMs;
        this.symbols = b.symbols.isEmpty() ? Collections.emptySet() : new HashSet<>(b.symbols);
        this.sectors = b.sectors.isEmpty() ? Collections.emptySet() : new HashSet<>(b.sectors);
        this.bigOrderThreshold = b.bigOrderThreshold;
        this.pushIntervalMs = b.pushIntervalMs;
        this.deltaEnabled = b.deltaEnabled;

        // 编译 DSL
        try {
            Lexer lexer = new Lexer(dsl);
            List<Lexer.Token> tokens = lexer.tokenize();
            Parser parser = new Parser(tokens);
            this.definition = parser.parseRule();
        } catch (Exception e) {
            throw new IllegalArgumentException("DSL compile failed for rule " + id + ": " + e.getMessage(), e);
        }
    }

    public String getId() { return id; }
    public String getName() { return name; }
    public String getDsl() { return dsl; }
    public long getWindowSizeMs() { return windowSizeMs; }
    public Set<String> getSymbols() { return Collections.unmodifiableSet(symbols); }
    public Set<String> getSectors() { return Collections.unmodifiableSet(sectors); }
    public long getBigOrderThreshold() { return bigOrderThreshold; }
    public long getPushIntervalMs() { return pushIntervalMs; }
    public boolean isDeltaEnabled() { return deltaEnabled; }
    public boolean isEnabled() { return enabled; }
    public void setEnabled(boolean v) { enabled = v; }

    public Parser.AggregateRuleDefinition getDefinition() { return definition; }

    public long getTotalTicksProcessed() { return totalTicksProcessed.get(); }
    public long getTotalAggregatesGenerated() { return totalAggregatesGenerated.get(); }
    public void incrementTicks() { totalTicksProcessed.incrementAndGet(); }
    public void incrementAggregates() { totalAggregatesGenerated.incrementAndGet(); }

    /**
     * 检查该规则是否关心某个 tick。
     */
    public boolean isRelevant(TickData tick) {
        if (!enabled) return false;
        if (!symbols.isEmpty() && !symbols.contains(tick.symbol)) return false;
        if (!sectors.isEmpty() && !sectors.contains(tick.sector)) return false;
        return definition.matches(tick);
    }

    /**
     * 获取某个 symbol 的最近快照 (用于 Delta 编码)。
     */
    public AggregateResult getLastSnapshot(String symbol) {
        return lastSnapshot.get(symbol);
    }

    public void setLastSnapshot(String symbol, AggregateResult result) {
        lastSnapshot.put(symbol, result);
    }

    public boolean shouldPush(long now) {
        return now - lastPushTs >= pushIntervalMs;
    }

    public void markPushed(long now) {
        lastPushTs = now;
    }

    // ============ Builder ============

    public static Builder builder() { return new Builder(); }

    public static final class Builder {
        private String id;
        private String name;
        private String dsl;
        private long windowSizeMs = 1000;
        private List<String> symbols = new ArrayList<>();
        private List<String> sectors = new ArrayList<>();
        private long bigOrderThreshold = 10000;
        private long pushIntervalMs = 1000;
        private boolean deltaEnabled = true;

        public Builder id(String id) { this.id = id; return this; }
        public Builder name(String name) { this.name = name; return this; }
        public Builder dsl(String dsl) { this.dsl = dsl; return this; }
        public Builder windowSizeMs(long ms) { this.windowSizeMs = ms; return this; }
        public Builder symbols(Collection<String> s) { this.symbols.addAll(s); return this; }
        public Builder sectors(Collection<String> s) { this.sectors.addAll(s); return this; }
        public Builder bigOrderThreshold(long v) { this.bigOrderThreshold = v; return this; }
        public Builder pushIntervalMs(long ms) { this.pushIntervalMs = ms; return this; }
        public Builder deltaEnabled(boolean v) { this.deltaEnabled = v; return this; }

        public AggregateRule build() {
            Objects.requireNonNull(id, "id");
            Objects.requireNonNull(dsl, "dsl");
            if (name == null) name = id;
            return new AggregateRule(this);
        }
    }
}
