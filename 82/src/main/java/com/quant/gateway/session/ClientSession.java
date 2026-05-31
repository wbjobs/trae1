package com.quant.gateway.session;

import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;

import java.util.List;

/**
 * 客户端会话抽象, 实现可以是 Netty ChannelWrapper, Mock, 或测试桩。
 */
public interface ClientSession {

    String getClientId();

    String remoteAddress();

    long connectTime();

    long lastActivity();

    /**
     * 异步推送一笔 tick (非阻塞)。
     * 实现需保证线程安全 (可能被多个分发线程同时调用)。
     */
    void pushTick(TickData tick);

    /**
     * 异步补发 (用于重连后, 批量推送)。
     */
    void pushReplay(List<TickData> ticks);

    /**
     * 推送控制消息 (订阅响应 / 心跳 / 降级通知)。
     */
    void pushControl(MsgType type, byte[] payload);

    boolean isActive();

    void close();

    /**
     * 断线重连时, 保留会话的最后一笔接收流水号, 用于补发窗口定位。
     */
    long lastAckSeq();
    void ackSeq(long seq);
}
