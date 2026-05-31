package com.quant.gateway.codec;

import com.quant.gateway.aggregate.AggregateResult;
import com.quant.gateway.aggregate.DeltaEncoder;
import com.quant.gateway.config.SectorMapping;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;

import java.nio.charset.StandardCharsets;

/**
 * 紧凑二进制 codec (代替 protobuf 生成代码，无需 protoc 构建)。
 *
 * Wire format (大端):
 *   - varint: 7bit per byte, MSB=1 表示后续还有字节
 *   - string: varint(len) + utf8 bytes
 *   - double: 8 bytes IEEE754
 *
 * TickData 编码:
 *   [1] varint MsgType.code
 *   [2] string symbol
 *   [3] string sector
 *   [4] varint timestamp
 *   [5] varint seq
 *   [6] double price
 *   [7] varint volume (zigzag)
 *   [8] varint turnover (zigzag)
 *   [9] string tradeFlag/orderSide
 *
 * SubscribeRequest 编码:
 *   [1] string clientId
 *   [2] varint subType: 0=SYMBOL 1=SECTOR 2=KIND 3=CANCEL
 *   [3] varint count
 *   [4..] string items
 *   [..] varint kinds mask bit: 1=TICK 2=ORDER 4=SNAPSHOT
 *
 * SubscribeResponse 编码:
 *   [1] varint code
 *   [2] string message
 *
 * 通用消息帧 (用于 WebSocket):
 *   [1] varint MsgType.code
 *   [2] varint payload length
 *   [3] payload bytes (由具体 MsgType 决定)
 */
public final class BinaryCodec {

    private BinaryCodec() {}

    // ============ varint / zigzag ============

    public static int writeVarInt(ByteBuf buf, long value) {
        int written = 0;
        long v = value & 0xFFFFFFFFFFFFFFFFL;
        while ((v & ~0x7FL) != 0) {
            buf.writeByte((int) ((v & 0x7F) | 0x80));
            v >>>= 7;
            written++;
        }
        buf.writeByte((int) (v & 0x7F));
        return written + 1;
    }

    public static long readVarInt(ByteBuf buf) {
        long result = 0;
        int shift = 0;
        byte b;
        do {
            b = buf.readByte();
            result |= (long) (b & 0x7F) << shift;
            shift += 7;
        } while ((b & 0x80) != 0);
        return result;
    }

    public static long zigZag(long v) { return (v << 1) ^ (v >> 63); }
    public static long unZigZag(long v) { return (v >>> 1) ^ -(v & 1); }

    public static void writeString(ByteBuf buf, String s) {
        byte[] bytes = s.getBytes(StandardCharsets.UTF_8);
        writeVarInt(buf, bytes.length);
        buf.writeBytes(bytes);
    }

    public static String readString(ByteBuf buf) {
        int len = (int) readVarInt(buf);
        byte[] bytes = new byte[len];
        buf.readBytes(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }

    // ============ TickData ============

    public static ByteBuf encodeTick(TickData tick) {
        ByteBuf buf = Unpooled.buffer(128);
        writeVarInt(buf, tick.msgType.code());
        writeString(buf, tick.symbol);
        writeString(buf, tick.sector);
        writeVarInt(buf, tick.timestamp);
        writeVarInt(buf, tick.seq);
        buf.writeLong(Double.doubleToRawLongBits(tick.price));
        writeVarInt(buf, zigZag(tick.volume));
        writeVarInt(buf, zigZag(tick.turnover));
        String side = tick.msgType == MsgType.TICK ? tick.tradeFlag : tick.orderSide;
        writeString(buf, side == null ? "" : side);
        return buf;
    }

    public static TickData decodeTick(ByteBuf buf) {
        MsgType type = MsgType.fromCode((int) readVarInt(buf));
        String symbol = readString(buf);
        String sector = readString(buf);
        long ts = readVarInt(buf);
        long seq = readVarInt(buf);
        double price = Double.longBitsToDouble(buf.readLong());
        long volume = unZigZag(readVarInt(buf));
        long turnover = unZigZag(readVarInt(buf));
        String side = readString(buf);
        TickData t = new TickData();
        t.msgType = type;
        t.symbol = symbol;
        t.sector = sector;
        t.timestamp = ts;
        t.seq = seq;
        t.price = price;
        t.volume = volume;
        t.turnover = turnover;
        if (type == MsgType.TICK) t.tradeFlag = side;
        else t.orderSide = side;
        return t;
    }

    public static int tickByteSize(TickData tick) {
        String side = tick.msgType == MsgType.TICK ? tick.tradeFlag : tick.orderSide;
        if (side == null) side = "";
        int size = varLen(tick.msgType.code())
                + stringLen(tick.symbol) + stringLen(tick.sector)
                + varLen(tick.timestamp) + varLen(tick.seq)
                + 8
                + varLen(zigZag(tick.volume)) + varLen(zigZag(tick.turnover))
                + stringLen(side);
        return size;
    }

    private static int varLen(long v) {
        int n = 0;
        v = v & 0xFFFFFFFFFFFFFFFFL;
        do { n++; v >>>= 7; } while (v != 0);
        return n;
    }

    private static int stringLen(String s) {
        int utf8Len = s.getBytes(StandardCharsets.UTF_8).length;
        return varLen(utf8Len) + utf8Len;
    }

    // ============ SubscribeRequest ============

    public static class SubscribeReq {
        public String clientId;
        public int subType; // 0 SYMBOL, 1 SECTOR, 2 KIND, 3 CANCEL
        public java.util.List<String> items = new java.util.ArrayList<>();
        public int kindsMask; // 1 TICK, 2 ORDER, 4 SNAPSHOT
    }

    public static ByteBuf encodeSubscribeReq(SubscribeReq req) {
        ByteBuf buf = Unpooled.buffer(64);
        writeString(buf, req.clientId);
        writeVarInt(buf, req.subType);
        writeVarInt(buf, req.items.size());
        for (String s : req.items) writeString(buf, s);
        writeVarInt(buf, req.kindsMask);
        return buf;
    }

    public static SubscribeReq decodeSubscribeReq(ByteBuf buf) {
        SubscribeReq req = new SubscribeReq();
        req.clientId = readString(buf);
        req.subType = (int) readVarInt(buf);
        int n = (int) readVarInt(buf);
        for (int i = 0; i < n; i++) req.items.add(readString(buf));
        req.kindsMask = (int) readVarInt(buf);
        return req;
    }

    // ============ SubscribeResponse ============

    public static ByteBuf encodeSubscribeResp(int code, String msg) {
        ByteBuf buf = Unpooled.buffer(32);
        writeVarInt(buf, code);
        writeString(buf, msg == null ? "" : msg);
        return buf;
    }

    // ============ Frame 封装 (WebSocket payload) ============

    public static ByteBuf wrapFrame(MsgType type, ByteBuf payload) {
        int payloadLen = payload.readableBytes();
        ByteBuf frame = Unpooled.buffer(2 + varLen(payloadLen) + payloadLen);
        writeVarInt(frame, type.code());
        writeVarInt(frame, payloadLen);
        frame.writeBytes(payload);
        return frame;
    }

    public static final class Frame {
        public MsgType type;
        public ByteBuf payload; // slice (不拥有)
    }

    public static Frame unwrapFrame(ByteBuf buf) {
        Frame f = new Frame();
        f.type = MsgType.fromCode((int) readVarInt(buf));
        int len = (int) readVarInt(buf);
        f.payload = buf.readSlice(len);
        return f;
    }

    // ============ helpers for TickData construction ============

    public static TickData newTick(String symbol, long seq, double price, long volume, String bs) {
        TickData t = new TickData();
        t.msgType = MsgType.TICK;
        t.symbol = symbol;
        t.sector = SectorMapping.sectorOf(symbol);
        t.timestamp = System.currentTimeMillis();
        t.seq = seq;
        t.price = price;
        t.volume = volume;
        t.turnover = (long) (price * volume);
        t.tradeFlag = bs;
        return t;
    }

    public static TickData newOrder(String symbol, long seq, double price, long volume, String side) {
        TickData t = new TickData();
        t.msgType = MsgType.ORDER;
        t.symbol = symbol;
        t.sector = SectorMapping.sectorOf(symbol);
        t.timestamp = System.currentTimeMillis();
        t.seq = seq;
        t.price = price;
        t.volume = volume;
        t.turnover = (long) (price * volume);
        t.orderSide = side;
        return t;
    }

    // ============ 聚合结果编解码 (Delta 压缩) ============

    /**
     * 编码聚合结果为二进制。
     * @param current 当前结果
     * @param previous 上一次推送的快照 (null 表示首次推送)
     */
    public static ByteBuf encodeAggregate(AggregateResult current, AggregateResult previous) {
        return DeltaEncoder.encode(current, previous);
    }

    /**
     * 解码聚合结果。
     * @param buf 二进制数据
     * @param previous 上一次结果 (null 表示完整编码)
     */
    public static AggregateResult decodeAggregate(ByteBuf buf, AggregateResult previous) {
        return DeltaEncoder.decode(buf, previous);
    }
}
