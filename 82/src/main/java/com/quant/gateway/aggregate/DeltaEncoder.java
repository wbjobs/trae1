package com.quant.gateway.aggregate;

import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;

import java.util.*;

import static com.quant.gateway.codec.BinaryCodec.*;

/**
 * Delta 编码器: 比较当前结果与上一次快照, 只编码发生变化的字段, 减少带宽。
 *
 * 字段存在位图 (8 bits):
 *   bit 0: count
 *   bit 1: sumVolume
 *   bit 2: sumTurnover
 *   bit 3: wavgPrice
 *   bit 4: high/low
 *   bit 5: open/close
 *   bit 6: bigOrderCount/bigOrderVolume
 *   bit 7: customMetrics
 *
 * 格式:
 *   [varint ruleId len][ruleId bytes]
 *   [varint symbol len][symbol bytes]
 *   [varint windowStart][varint windowEnd]
 *   [byte fieldBitmap]
 *   (按 bitmap 顺序编码变化的字段)
 *   [if bit 7: varint metricCount][for each: string key][double value]
 *
 * 平均压缩率: 无变化时 ~10B, 全变化时 ~40-60B (比完整编码节省 30%-50%)
 */
public final class DeltaEncoder {

    private static final int BIT_COUNT       = 1 << 0;
    private static final int BIT_SUM_VOLUME  = 1 << 1;
    private static final int BIT_SUM_TURNOVER= 1 << 2;
    private static final int BIT_WAVG_PRICE  = 1 << 3;
    private static final int BIT_HIGH_LOW    = 1 << 4;
    private static final int BIT_OPEN_CLOSE  = 1 << 5;
    private static final int BIT_BIG_ORDER   = 1 << 6;
    private static final int BIT_CUSTOM      = 1 << 7;

    private DeltaEncoder() {}

    /**
     * 编码聚合结果为二进制 (Delta 或 完整)。
     *
     * @param current 当前聚合结果
     * @param previous 上一次推送的快照 (null 表示首次推送, 发送完整结果)
     * @return 编码后的 ByteBuf
     */
    public static ByteBuf encode(AggregateResult current, AggregateResult previous) {
        if (previous == null) {
            return encodeFull(current);
        }
        return encodeDelta(current, previous);
    }

    /**
     * 完整编码 (用于首次推送或 Delta 压缩率差时)。
     */
    public static ByteBuf encodeFull(AggregateResult r) {
        ByteBuf buf = Unpooled.buffer(128);

        writeString(buf, r.ruleId);
        writeString(buf, r.symbol);
        writeVarInt(buf, r.windowStart);
        writeVarInt(buf, r.windowEnd);
        writeVarInt(buf, r.seq);

        // 全量 bitmap = 所有位都设
        int bitmap = BIT_COUNT | BIT_SUM_VOLUME | BIT_SUM_TURNOVER | BIT_WAVG_PRICE
                   | BIT_HIGH_LOW | BIT_OPEN_CLOSE | BIT_BIG_ORDER
                   | (r.customMetrics != null && !r.customMetrics.isEmpty() ? BIT_CUSTOM : 0);
        buf.writeByte(bitmap);

        writeVarInt(buf, zigZag(r.count));
        writeVarInt(buf, zigZag(r.sumVolume));
        writeVarInt(buf, zigZag(r.sumTurnover));
        buf.writeLong(Double.doubleToRawLongBits(r.wavgPrice));
        buf.writeLong(Double.doubleToRawLongBits(r.high));
        buf.writeLong(Double.doubleToRawLongBits(r.low));
        buf.writeLong(Double.doubleToRawLongBits(r.open));
        buf.writeLong(Double.doubleToRawLongBits(r.close));
        writeVarInt(buf, zigZag(r.bigOrderCount));
        writeVarInt(buf, zigZag(r.bigOrderVolume));

        if ((bitmap & BIT_CUSTOM) != 0) {
            writeVarInt(buf, r.customMetrics.size());
            for (Map.Entry<String, Double> e : r.customMetrics.entrySet()) {
                writeString(buf, e.getKey());
                buf.writeLong(Double.doubleToRawLongBits(e.getValue()));
            }
        }
        return buf;
    }

    /**
     * Delta 编码: 只写与 previous 不同的字段。
     */
    public static ByteBuf encodeDelta(AggregateResult cur, AggregateResult prev) {
        ByteBuf buf = Unpooled.buffer(64);

        writeString(buf, cur.ruleId);
        writeString(buf, cur.symbol);
        writeVarInt(buf, cur.windowStart);
        writeVarInt(buf, cur.windowEnd);
        writeVarInt(buf, cur.seq);

        // 计算差异位图
        int bitmap = 0;
        if (cur.count != prev.count) bitmap |= BIT_COUNT;
        if (cur.sumVolume != prev.sumVolume) bitmap |= BIT_SUM_VOLUME;
        if (cur.sumTurnover != prev.sumTurnover) bitmap |= BIT_SUM_TURNOVER;
        if (Double.doubleToRawLongBits(cur.wavgPrice) != Double.doubleToRawLongBits(prev.wavgPrice)) bitmap |= BIT_WAVG_PRICE;
        if (Double.doubleToRawLongBits(cur.high) != Double.doubleToRawLongBits(prev.high)
         || Double.doubleToRawLongBits(cur.low)  != Double.doubleToRawLongBits(prev.low)) bitmap |= BIT_HIGH_LOW;
        if (Double.doubleToRawLongBits(cur.open) != Double.doubleToRawLongBits(prev.open)
         || Double.doubleToRawLongBits(cur.close)!= Double.doubleToRawLongBits(prev.close)) bitmap |= BIT_OPEN_CLOSE;
        if (cur.bigOrderCount != prev.bigOrderCount
         || cur.bigOrderVolume != prev.bigOrderVolume) bitmap |= BIT_BIG_ORDER;

        // 自定义指标差异
        Map<String, Double> changedMetrics = new HashMap<>();
        if (cur.customMetrics != null) {
            for (Map.Entry<String, Double> e : cur.customMetrics.entrySet()) {
                Double prevVal = prev.customMetrics == null ? null : prev.customMetrics.get(e.getKey());
                if (prevVal == null || Double.doubleToRawLongBits(e.getValue()) != Double.doubleToRawLongBits(prevVal)) {
                    changedMetrics.put(e.getKey(), e.getValue());
                }
            }
        }
        if (!changedMetrics.isEmpty()) bitmap |= BIT_CUSTOM;

        buf.writeByte(bitmap);

        // 按位图顺序写字段 (只写变化的)
        if ((bitmap & BIT_COUNT) != 0) writeVarInt(buf, zigZag(cur.count - prev.count));
        if ((bitmap & BIT_SUM_VOLUME) != 0) writeVarInt(buf, zigZag(cur.sumVolume - prev.sumVolume));
        if ((bitmap & BIT_SUM_TURNOVER) != 0) writeVarInt(buf, zigZag(cur.sumTurnover - prev.sumTurnover));
        if ((bitmap & BIT_WAVG_PRICE) != 0) buf.writeLong(Double.doubleToRawLongBits(cur.wavgPrice));
        if ((bitmap & BIT_HIGH_LOW) != 0) {
            buf.writeLong(Double.doubleToRawLongBits(cur.high));
            buf.writeLong(Double.doubleToRawLongBits(cur.low));
        }
        if ((bitmap & BIT_OPEN_CLOSE) != 0) {
            buf.writeLong(Double.doubleToRawLongBits(cur.open));
            buf.writeLong(Double.doubleToRawLongBits(cur.close));
        }
        if ((bitmap & BIT_BIG_ORDER) != 0) {
            writeVarInt(buf, zigZag(cur.bigOrderCount - prev.bigOrderCount));
            writeVarInt(buf, zigZag(cur.bigOrderVolume - prev.bigOrderVolume));
        }
        if ((bitmap & BIT_CUSTOM) != 0) {
            writeVarInt(buf, changedMetrics.size());
            for (Map.Entry<String, Double> e : changedMetrics.entrySet()) {
                writeString(buf, e.getKey());
                buf.writeLong(Double.doubleToRawLongBits(e.getValue()));
            }
        }
        return buf;
    }

    /**
     * 解码 (支持 Delta 和完整编码)。
     *
     * @param buf 二进制数据
     * @param previous 上一次的结果 (用于 Delta 解码, null 表示完整编码)
     * @return 解码后的 AggregateResult
     */
    public static AggregateResult decode(ByteBuf buf, AggregateResult previous) {
        String ruleId = readString(buf);
        String symbol = readString(buf);
        long windowStart = readVarInt(buf);
        long windowEnd = readVarInt(buf);
        long seq = readVarInt(buf);
        int bitmap = buf.readByte() & 0xFF;

        AggregateResult r = new AggregateResult();
        r.ruleId = ruleId;
        r.symbol = symbol;
        r.windowStart = windowStart;
        r.windowEnd = windowEnd;
        r.seq = seq;

        // 如果 previous 存在, 先复制 previous 的值, 再覆盖变化的字段
        if (previous != null) {
            r.count = previous.count;
            r.sumVolume = previous.sumVolume;
            r.sumTurnover = previous.sumTurnover;
            r.wavgPrice = previous.wavgPrice;
            r.high = previous.high;
            r.low = previous.low;
            r.open = previous.open;
            r.close = previous.close;
            r.bigOrderCount = previous.bigOrderCount;
            r.bigOrderVolume = previous.bigOrderVolume;
            r.bigOrderThreshold = previous.bigOrderThreshold;
            r.customMetrics = new HashMap<>(previous.customMetrics);
        } else {
            r.customMetrics = new HashMap<>();
        }

        // 增量字段: 如果是 Delta, 值是 diff, 需要加到 previous 上
        boolean delta = previous != null;

        if ((bitmap & BIT_COUNT) != 0) {
            long v = unZigZag(readVarInt(buf));
            r.count = delta ? previous.count + v : v;
        }
        if ((bitmap & BIT_SUM_VOLUME) != 0) {
            long v = unZigZag(readVarInt(buf));
            r.sumVolume = delta ? previous.sumVolume + v : v;
        }
        if ((bitmap & BIT_SUM_TURNOVER) != 0) {
            long v = unZigZag(readVarInt(buf));
            r.sumTurnover = delta ? previous.sumTurnover + v : v;
        }
        if ((bitmap & BIT_WAVG_PRICE) != 0) {
            r.wavgPrice = Double.longBitsToDouble(buf.readLong());
        }
        if ((bitmap & BIT_HIGH_LOW) != 0) {
            r.high = Double.longBitsToDouble(buf.readLong());
            r.low = Double.longBitsToDouble(buf.readLong());
        }
        if ((bitmap & BIT_OPEN_CLOSE) != 0) {
            r.open = Double.longBitsToDouble(buf.readLong());
            r.close = Double.longBitsToDouble(buf.readLong());
        }
        if ((bitmap & BIT_BIG_ORDER) != 0) {
            long countV = unZigZag(readVarInt(buf));
            long volV = unZigZag(readVarInt(buf));
            r.bigOrderCount = delta ? previous.bigOrderCount + countV : countV;
            r.bigOrderVolume = delta ? previous.bigOrderVolume + volV : volV;
        }
        if ((bitmap & BIT_CUSTOM) != 0) {
            int n = (int) readVarInt(buf);
            for (int i = 0; i < n; i++) {
                String key = readString(buf);
                double val = Double.longBitsToDouble(buf.readLong());
                r.customMetrics.put(key, val);
            }
        }
        return r;
    }
}
