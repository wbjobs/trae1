package com.quant.gateway.aggregate;

import com.quant.gateway.codec.TickData;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 滑动窗口聚合结果。
 *
 * 字段设计:
 *   - 基础聚合: count, sumVolume, sumTurnover, avgPrice, wavgPrice (加权均价)
 *   - 大单统计: bigOrderCount (单笔成交量 > threshold), bigOrderVolume
 *   - 价格区间: high, low, open, close (窗口首尾价)
 *   - 维度键: ruleId, symbol, windowStart, windowEnd
 */
public final class AggregateResult {

    public String ruleId;
    public String symbol;
    public String sector;
    public long windowStart;
    public long windowEnd;
    public long seq;

    // 基础聚合
    public long count;
    public long sumVolume;
    public long sumTurnover;
    public double avgPrice;
    public double wavgPrice;

    // 价格极值
    public double high;
    public double low;
    public double open;
    public double close;

    // 大单统计
    public long bigOrderCount;
    public long bigOrderVolume;
    public long bigOrderThreshold;

    // 自定义指标 (name -> value)
    public Map<String, Double> customMetrics = new ConcurrentHashMap<>();

    public AggregateResult() {}

    public AggregateResult(String ruleId, TickData first, long windowStart, long windowSizeMs, long bigOrderThreshold) {
        this.ruleId = ruleId;
        this.symbol = first.symbol;
        this.sector = first.sector;
        this.windowStart = windowStart;
        this.windowEnd = windowStart + windowSizeMs;
        this.bigOrderThreshold = bigOrderThreshold;
        this.open = first.price;
        this.high = first.price;
        this.low = first.price;
    }

    /**
     * 合并一笔 tick 到当前结果 (增量更新)。
     * @param t 新 tick
     * @return true 表示有变化, false 无变化
     */
    public boolean update(TickData t) {
        if (!symbol.equals(t.symbol)) return false;

        count++;
        sumVolume += t.volume;
        sumTurnover += t.turnover;
        close = t.price;
        if (t.price > high) high = t.price;
        if (t.price < low) low = t.price;

        // 大单统计
        if (t.volume >= bigOrderThreshold) {
            bigOrderCount++;
            bigOrderVolume += t.volume;
        }

        // 均价 (简单)
        avgPrice = sumTurnover / (double) Math.max(1, sumVolume);
        // 加权均价 (按成交量加权)
        wavgPrice = sumTurnover / (double) Math.max(1, sumVolume);

        return true;
    }

    /**
     * 合并另一个窗口的结果 (用于跨窗口聚合)。
     */
    public void merge(AggregateResult other) {
        count += other.count;
        sumVolume += other.sumVolume;
        sumTurnover += other.sumTurnover;
        bigOrderCount += other.bigOrderCount;
        bigOrderVolume += other.bigOrderVolume;
        if (other.high > high) high = other.high;
        if (other.low < low) low = other.low;
        if (other.windowStart < windowStart) {
            windowStart = other.windowStart;
            open = other.open;
        }
        if (other.windowEnd > windowEnd) {
            windowEnd = other.windowEnd;
            close = other.close;
        }
        avgPrice = sumTurnover / (double) Math.max(1, sumVolume);
        wavgPrice = sumTurnover / (double) Math.max(1, sumVolume);
    }

    @Override
    public String toString() {
        return String.format(
                "Aggregate{rule=%s, sym=%s, win=[%d-%d], cnt=%d, vol=%d, wavg=%.2f, high=%.2f, low=%.2f}",
                ruleId, symbol, windowStart, windowEnd, count, sumVolume, wavgPrice, high, low);
    }
}
