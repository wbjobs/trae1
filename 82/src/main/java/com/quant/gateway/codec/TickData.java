package com.quant.gateway.codec;

/**
 * 行情数据 POJO (逐笔成交 / 逐笔委托)。
 *
 * 设计说明:
 *  - 与 market.proto 中 TickData 语义一致
 *  - 内存中使用 POJO, 发送前由 BinaryCodec 编码为紧凑二进制,
 *    其字段布局/压缩方式与 protobuf wire format 兼容 (varint + zigzag)。
 *  - 单对象约 80B, 10 万笔/秒 ≈ 8MB/s 写带宽, 可轻松在 1000 连接扇出前做聚合。
 */
public final class TickData {

    public MsgType msgType;   // TICK / ORDER
    public String  symbol;    // 股票代码
    public String  sector;    // 所属板块
    public long    timestamp; // 源时间戳
    public long    seq;       // 全局递增流水号 (网关分配)
    public double  price;
    public long    volume;
    public long    turnover;
    public String  tradeFlag; // B / S (逐笔成交)
    public String  orderSide; // BID / ASK (逐笔委托)

    @Override
    public String toString() {
        return "Tick{" + msgType + "," + symbol + "," + seq +
               ",p=" + price + ",v=" + volume +
               (tradeFlag != null ? "," + tradeFlag : "") +
               (orderSide != null ? "," + orderSide : "") + "}";
    }
}
