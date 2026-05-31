package com.quant.gateway.aggregate;

import com.quant.gateway.codec.BinaryCodec;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.session.ClientSession;
import com.quant.gateway.session.NettyClientSession;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 聚合推送管理器:
 *   - 维护客户端对聚合规则的订阅
 *   - 监听聚合引擎的 onAggregate 事件, 将结果推送给所有订阅客户端
 *   - 自动应用 Delta 编码 (只发送变化的字段)
 *   - 维护每个客户端上一次收到的快照, 用于 Delta 编码
 */
public final class AggregatePusher implements SlidingWindowAggregator.AggregateListener {

    private static final Logger log = LoggerFactory.getLogger(AggregatePusher.class);

    /** ruleId -> Set<clientId> */
    private final ConcurrentHashMap<String, Set<String>> ruleSubscribers = new ConcurrentHashMap<>();

    /** clientId -> (ruleId -> lastSnapshot) */
    private final ConcurrentHashMap<String, ConcurrentHashMap<String, AggregateResult>> clientLastSnapshots = new ConcurrentHashMap<>();

    private final SlidingWindowAggregator aggregator;

    public AggregatePusher(SlidingWindowAggregator aggregator) {
        this.aggregator = aggregator;
        this.aggregator.setListener(this);
    }

    /**
     * 客户端订阅聚合规则。
     */
    public void subscribe(String clientId, String ruleId) {
        ruleSubscribers.computeIfAbsent(ruleId, k -> ConcurrentHashMap.newKeySet()).add(clientId);
        clientLastSnapshots.computeIfAbsent(clientId, k -> new ConcurrentHashMap<>());
        log.info("client {} subscribed to aggregate rule {}", clientId, ruleId);
    }

    /**
     * 客户端取消订阅。
     */
    public void unsubscribe(String clientId, String ruleId) {
        Set<String> subs = ruleSubscribers.get(ruleId);
        if (subs != null) {
            subs.remove(clientId);
            if (subs.isEmpty()) ruleSubscribers.remove(ruleId);
        }
        Map<String, AggregateResult> snaps = clientLastSnapshots.get(clientId);
        if (snaps != null) snaps.remove(ruleId);
        log.info("client {} unsubscribed from aggregate rule {}", clientId, ruleId);
    }

    /**
     * 客户端断开时, 清理所有订阅。
     */
    public void onClientDisconnect(String clientId) {
        for (Set<String> subs : ruleSubscribers.values()) {
            subs.remove(clientId);
        }
        clientLastSnapshots.remove(clientId);
        ruleSubscribers.values().removeIf(Set::isEmpty);
    }

    /**
     * 获取某规则的订阅客户端数量。
     */
    public int subscriberCount(String ruleId) {
        Set<String> subs = ruleSubscribers.get(ruleId);
        return subs == null ? 0 : subs.size();
    }

    /**
     * 获取某客户端的所有订阅规则。
     */
    public Set<String> getSubscriptions(String clientId) {
        Set<String> out = ConcurrentHashMap.newKeySet();
        for (Map.Entry<String, Set<String>> e : ruleSubscribers.entrySet()) {
            if (e.getValue().contains(clientId)) out.add(e.getKey());
        }
        return out;
    }

    /**
     * 聚合引擎回调: 新的聚合结果产生。
     */
    @Override
    public void onAggregate(AggregateRule rule, AggregateResult result) {
        Set<String> subs = ruleSubscribers.get(rule.getId());
        if (subs == null || subs.isEmpty() || result.symbol == null) return;

        for (String clientId : subs) {
            ClientSession session = findSession(clientId);
            if (session == null || !session.isActive()) continue;

            // 获取上次快照用于 Delta 编码
            Map<String, AggregateResult> snaps = clientLastSnapshots.get(clientId);
            AggregateResult prev = snaps == null ? null : snaps.get(rule.getId() + ":" + result.symbol);

            // 编码 (Delta 或完整)
            ByteBuf payload = BinaryCodec.encodeAggregate(result, prev);
            ByteBuf frame = BinaryCodec.wrapFrame(MsgType.AGGREGATE, payload);
            payload.release();

            // 发送
            if (session instanceof NettyClientSession) {
                NettyClientSession ns = (NettyClientSession) session;
                sendAggregateToSession(ns, frame, result, rule, prev);
            }
        }

        // 清理空的订阅集合
        ruleSubscribers.values().removeIf(Set::isEmpty);
    }

    private void sendAggregateToSession(NettyClientSession session, ByteBuf frame,
                                        AggregateResult result, AggregateRule rule,
                                        AggregateResult prev) {
        if (!session.isActive()) {
            frame.release();
            return;
        }

        // 检查背压: 如果会话背压, 聚合结果直接丢弃 (下次推送时 Delta 会自动包含差值)
        if (session.isBackPressured()) {
            frame.release();
            return;
        }

        if (session.channel().eventLoop().inEventLoop()) {
            doSendAggregate(session, frame, result, rule);
        } else {
            session.channel().eventLoop().execute(() -> doSendAggregate(session, frame, result, rule));
        }

        // 记录客户端快照, 用于下次 Delta 编码
        Map<String, AggregateResult> snaps = clientLastSnapshots.get(session.getClientId());
        if (snaps != null) {
            snaps.put(rule.getId() + ":" + result.symbol, result);
        }
    }

    private void doSendAggregate(NettyClientSession session, ByteBuf frame, AggregateResult result, AggregateRule rule) {
        if (!session.isActive()) {
            frame.release();
            return;
        }
        if (!session.channel().isWritable()) {
            frame.release();
            return;
        }
        int frameBytes = frame.readableBytes();
        session.channel().writeAndFlush(new io.netty.handler.codec.http.websocketx.BinaryWebSocketFrame(frame))
                .addListener(f -> {
                    if (!f.isSuccess()) {
                        log.debug("[{}] aggregate write fail: {}", session.getClientId(), f.cause().getMessage());
                    }
                });
    }

    /** 查找会话 (由外部注入 session registry 的引用) */
    private ClientSession findSession(String clientId) {
        return sessionFinder == null ? null : sessionFinder.find(clientId);
    }

    /** 会话查找器接口 */
    public interface SessionFinder {
        ClientSession find(String clientId);
    }

    private SessionFinder sessionFinder;

    public void setSessionFinder(SessionFinder sessionFinder) {
        this.sessionFinder = sessionFinder;
    }

    public Map<String, Set<String>> allSubscriptions() {
        Map<String, Set<String>> out = new HashMap<>();
        for (Map.Entry<String, Set<String>> e : ruleSubscribers.entrySet()) {
            out.put(e.getKey(), new HashSet<>(e.getValue()));
        }
        return out;
    }
}
