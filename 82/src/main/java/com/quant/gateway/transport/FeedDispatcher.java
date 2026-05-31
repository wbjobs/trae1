package com.quant.gateway.transport;

import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;
import com.quant.gateway.session.ClientSession;

import java.util.List;

/**
 * 行情分发器: 接收来自 UDP 的 TickData, 根据每个 ClientSession 的订阅过滤,
 * 并通过会话推送。
 *
 * 设计要点:
 *   - 不持有订阅结构, 订阅由 SubscriptionManager 管理;
 *   - 不推送至网络, 网络写由 ClientSession 负责 (批量 + back-pressure)。
 */
public interface FeedDispatcher {

    /**
     * 接收到一笔新 tick。
     * 实现需保证非阻塞, 网络写应在会话内部异步完成。
     */
    void dispatch(TickData tick);

    /**
     * 行情源断流 -> 降级, 实现应向所有在线客户端发送降级通知。
     */
    void onFeedDown();

    /**
     * 行情源恢复 -> 退出降级。
     */
    void onFeedUp();

    /**
     * 向单个客户端补发 (重连后使用)。
     */
    void replay(ClientSession session, List<TickData> ticks);

    /**
     * 推送一个通用消息 (订阅响应/心跳/降级通知等)。
     */
    void pushControl(ClientSession session, MsgType type, byte[] payload);
}
