package com.quant.gateway.session;

import com.quant.gateway.codec.BinaryCodec;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFutureListener;
import io.netty.handler.codec.http.websocketx.BinaryWebSocketFrame;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 基于 Netty Channel 的 WebSocket 客户端会话。
 *
 * 背压与降级策略:
 *   - 每连接独立高低水位 (HIGH=64KB, LOW=32KB), 通过 ChannelOption.WRITE_BUFFER_WATER_MARK 配置
 *   - 当写缓冲区 > HIGH: 进入 "消费降级" 模式
 *       1) 停止实时逐条推送, 改为每秒批量聚合一次
 *       2) 向客户端发送 DEGRADE_NOTICE (包含当前积压字节数)
 *       3) 通知上游分发器暂停该客户端 (背压传播)
 *   - 当写缓冲区 < LOW: 退出降级模式, 恢复实时推送, 通知上游恢复
 *   - 缓冲区写队列中若积压严重, 额外统计 dropped 计数, 不影响其他客户端
 *
 * 线程模型:
 *   - 所有 tick 先进入本地聚合队列 (ArrayDeque), 由 EventLoop 内的定时任务 flush
 *   - 降级模式下, 聚合队列满时丢弃最旧的 tick (先进先丢, 保证新数据优先)
 */
public final class NettyClientSession implements ClientSession {

    private static final Logger log = LoggerFactory.getLogger(NettyClientSession.class);

    public static final int HIGH_WATER_MARK = 64 * 1024;
    public static final int LOW_WATER_MARK  = 32 * 1024;

    private static final int AGGREGATE_BATCH_MAX = 500;
    private static final long AGGREGATE_INTERVAL_MS = 1000;

    private final String clientId;
    private final Channel channel;
    private final long connectTime;
    private final AtomicLong lastActivity = new AtomicLong(System.currentTimeMillis());
    private final AtomicLong ackSeq = new AtomicLong(0);
    private final AtomicLong dropped = new AtomicLong(0);

    // 背压状态
    private final AtomicBoolean backPressured = new AtomicBoolean(false);
    private final AtomicInteger pendingBytes = new AtomicInteger(0);
    private final AtomicLong lastAggregateFlush = new AtomicLong(0);
    private final AtomicLong backPressureStartTs = new AtomicLong(0);
    private final AtomicLong backPressureEvents = new AtomicLong(0);

    // 聚合队列 (降级模式下使用)
    private final java.util.Deque<TickData> aggregateQueue = new java.util.ArrayDeque<>(AGGREGATE_BATCH_MAX + 1);

    // 监听器回调 (背压传播到上游)
    private volatile BackPressureListener bpListener;

    public NettyClientSession(String clientId, Channel channel) {
        this.clientId = clientId;
        this.channel = channel;
        this.connectTime = System.currentTimeMillis();
    }

    public void setBackPressureListener(BackPressureListener listener) {
        this.bpListener = listener;
    }

    @Override public String getClientId() { return clientId; }
    @Override public String remoteAddress() { return String.valueOf(channel.remoteAddress()); }
    @Override public long connectTime() { return connectTime; }
    @Override public long lastActivity() { return lastActivity.get(); }
    @Override public boolean isActive() { return channel.isActive(); }
    @Override public void close() { channel.close(); }
    @Override
    public long lastAckSeq() { return ackSeq.get(); }
    @Override
    public void ackSeq(long seq) { ackSeq.accumulateAndGet(seq, Math::max); lastActivity.set(System.currentTimeMillis()); }

    /** 访问内部 Channel (用于聚合推送等高级场景) */
    public io.netty.channel.Channel channel() { return channel; }

    /**
     * 由 WebSocketHandler 在 channelWritabilityChanged 事件中调用。
     * 根据当前 writable 状态切换背压模式。
     */
    public void onWritabilityChanged(boolean writable) {
        if (writable) {
            if (backPressured.compareAndSet(true, false)) {
                long duration = System.currentTimeMillis() - backPressureStartTs.get();
                backPressureEvents.incrementAndGet();
                log.info("[{}] back pressure released (duration={}ms, dropped={})",
                        clientId, duration, dropped.get());
                // 恢复实时推送
                flushAggregateBatch();
                if (bpListener != null) bpListener.onResume(this);
            }
        } else {
            if (backPressured.compareAndSet(false, true)) {
                backPressureStartTs.set(System.currentTimeMillis());
                log.warn("[{}] back pressure active (pendingBytes={}, channelWritable=false)",
                        clientId, pendingBytes.get());
                // 向客户端发送警告
                String warn = String.format("SLOW_CONSUMER pending=%dB", pendingBytes.get());
                pushControl(MsgType.DEGRADE_NOTICE, warn.getBytes());
                // 通知上游暂停该客户端
                if (bpListener != null) bpListener.onPause(this);
            }
        }
    }

    @Override
    public void pushTick(TickData tick) {
        if (!channel.isActive()) return;

        if (backPressured.get()) {
            // 降级模式: 聚合到本地队列, 每秒 flush 一次
            enqueueAggregate(tick);
            return;
        }

        if (!channel.isWritable()) {
            // 即将进入背压, 先入聚合队列避免直接丢弃
            enqueueAggregate(tick);
            return;
        }

        // 正常实时推送
        doWriteTick(tick);
    }

    private void enqueueAggregate(TickData tick) {
        synchronized (aggregateQueue) {
            if (aggregateQueue.size() >= AGGREGATE_BATCH_MAX) {
                aggregateQueue.pollFirst();
                dropped.incrementAndGet();
            }
            aggregateQueue.addLast(tick);
        }
        // 检查是否到 flush 时间
        long now = System.currentTimeMillis();
        long last = lastAggregateFlush.get();
        if (now - last >= AGGREGATE_INTERVAL_MS) {
            if (lastAggregateFlush.compareAndSet(last, now)) {
                scheduleFlush();
            }
        }
    }

    private void scheduleFlush() {
        if (channel.eventLoop().inEventLoop()) {
            flushAggregateBatch();
        } else {
            channel.eventLoop().execute(this::flushAggregateBatch);
        }
    }

    private void flushAggregateBatch() {
        if (!channel.isActive()) return;
        List<TickData> batch;
        synchronized (aggregateQueue) {
            if (aggregateQueue.isEmpty()) return;
            batch = new ArrayList<>(aggregateQueue);
            aggregateQueue.clear();
        }
        // 批量 write, 最后一次 flush
        int size = batch.size();
        for (int i = 0; i < size; i++) {
            TickData t = batch.get(i);
            ByteBuf payload = BinaryCodec.encodeTick(t);
            ByteBuf frame = BinaryCodec.wrapFrame(t.msgType, payload);
            payload.release();
            int frameBytes = frame.readableBytes();
            if (i == size - 1) {
                channel.writeAndFlush(new BinaryWebSocketFrame(frame))
                        .addListener((ChannelFutureListener) f -> {
                            if (f.isSuccess()) {
                                pendingBytes.addAndGet(-frameBytes);
                            }
                            if (!f.isSuccess()) {
                                log.debug("[{}] flush fail: {}", clientId, f.cause().getMessage());
                            }
                        });
            } else {
                channel.write(new BinaryWebSocketFrame(frame));
            }
            pendingBytes.addAndGet(frameBytes);
            ackSeq(t.seq);
        }
        lastAggregateFlush.set(System.currentTimeMillis());
    }

    private void doWriteTick(TickData tick) {
        ByteBuf payload = BinaryCodec.encodeTick(tick);
        ByteBuf frame = BinaryCodec.wrapFrame(tick.msgType, payload);
        payload.release();
        int frameBytes = frame.readableBytes();
        pendingBytes.addAndGet(frameBytes);

        if (channel.eventLoop().inEventLoop()) {
            writeFrame(frame, frameBytes);
        } else {
            channel.eventLoop().execute(() -> writeFrame(frame, frameBytes));
        }
        ackSeq(tick.seq);
    }

    private void writeFrame(ByteBuf frame, int frameBytes) {
        if (!channel.isActive()) {
            frame.release();
            pendingBytes.addAndGet(-frameBytes);
            return;
        }
        channel.writeAndFlush(new BinaryWebSocketFrame(frame))
                .addListener((ChannelFutureListener) f -> {
                    if (f.isSuccess()) {
                        pendingBytes.addAndGet(-frameBytes);
                    }
                    if (!f.isSuccess()) {
                        log.debug("[{}] write fail: {}", clientId, f.cause().getMessage());
                    }
                });
    }

    @Override
    public void pushReplay(List<TickData> ticks) {
        if (!channel.isActive() || ticks.isEmpty()) return;
        // REPLAY_START
        ByteBuf start = BinaryCodec.wrapFrame(MsgType.REPLAY_START, Unpooled.EMPTY_BUFFER);
        channel.write(new BinaryWebSocketFrame(start));
        int totalBytes = 0;
        for (TickData t : ticks) {
            ByteBuf payload = BinaryCodec.encodeTick(t);
            ByteBuf frame = BinaryCodec.wrapFrame(t.msgType, payload);
            payload.release();
            totalBytes += frame.readableBytes();
            channel.write(new BinaryWebSocketFrame(frame));
        }
        ByteBuf end = BinaryCodec.wrapFrame(MsgType.REPLAY_END, Unpooled.EMPTY_BUFFER);
        final int bytes = totalBytes;
        channel.writeAndFlush(new BinaryWebSocketFrame(end))
                .addListener((ChannelFutureListener) f -> {
                    if (f.isSuccess()) {
                        pendingBytes.addAndGet(-bytes);
                    }
                });
    }

    @Override
    public void pushControl(MsgType type, byte[] payload) {
        if (!channel.isActive()) return;
        ByteBuf p = Unpooled.wrappedBuffer(payload);
        ByteBuf frame = BinaryCodec.wrapFrame(type, p);
        p.release();
        int frameBytes = frame.readableBytes();
        pendingBytes.addAndGet(frameBytes);
        if (channel.eventLoop().inEventLoop()) {
            writeFrame(frame, frameBytes);
        } else {
            channel.eventLoop().execute(() -> writeFrame(frame, frameBytes));
        }
    }

    // ============ 状态查询 (供 REST API) ============

    public boolean isBackPressured() { return backPressured.get(); }

    public int pendingBytes() { return Math.max(0, pendingBytes.get()); }

    public long droppedCount() { return dropped.get(); }

    public long backPressureEvents() { return backPressureEvents.get(); }

    public long backPressureDurationMs() {
        if (!backPressured.get()) return 0;
        return System.currentTimeMillis() - backPressureStartTs.get();
    }

    public int aggregateQueueSize() {
        synchronized (aggregateQueue) { return aggregateQueue.size(); }
    }

    public double bufferUsagePercent() {
        int pending = Math.max(0, pendingBytes.get());
        return Math.min(100.0, (pending * 100.0) / HIGH_WATER_MARK);
    }

    /**
     * 背压事件监听器, 用于传播到上游分发器。
     */
    public interface BackPressureListener {
        void onPause(NettyClientSession session);
        void onResume(NettyClientSession session);
    }
}
