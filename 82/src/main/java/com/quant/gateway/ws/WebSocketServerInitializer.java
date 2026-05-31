package com.quant.gateway.ws;

import com.quant.gateway.config.GatewayConfig;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.session.NettyClientSession;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import com.quant.gateway.aggregate.AggregatePusher;
import io.netty.channel.Channel;
import io.netty.channel.ChannelInitializer;
import io.netty.channel.WriteBufferWaterMark;
import io.netty.handler.codec.http.HttpObjectAggregator;
import io.netty.handler.codec.http.HttpServerCodec;
import io.netty.handler.codec.http.websocketx.WebSocketServerHandshakerFactory;
import io.netty.handler.timeout.IdleStateHandler;

import java.util.concurrent.TimeUnit;

/**
 * WebSocket 服务端初始化器:
 *   - 为每个新连接独立设置写缓冲区高低水位 (HIGH=64KB, LOW=32KB)
 *   - HTTP 升级到 WebSocket
 *   - 处理 SUBSCRIBE_REQ / HELLO 等控制消息
 *   - 心跳检测 (服务端主动 HEARTBEAT)
 */
public final class WebSocketServerInitializer extends ChannelInitializer<Channel> {

    private final WebSocketServerHandshakerFactory wsFactory;
    private final SessionRegistry sessions;
    private final SubscriptionManager subscriptions;
    private final MarketCache cache;
    private final DefaultFeedDispatcher dispatcher;
    private final AggregatePusher aggregatePusher;

    public WebSocketServerInitializer(SessionRegistry sessions,
                                      SubscriptionManager subscriptions,
                                      MarketCache cache,
                                      DefaultFeedDispatcher dispatcher,
                                      AggregatePusher aggregatePusher) {
        this.wsFactory = new WebSocketServerHandshakerFactory(
                "ws://localhost:" + GatewayConfig.WS_PORT + GatewayConfig.WS_PATH, null, true);
        this.sessions = sessions;
        this.subscriptions = subscriptions;
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregatePusher = aggregatePusher;
    }

    @Override
    protected void initChannel(Channel ch) {
        // 每连接独立的高低水位 (64KB / 32KB)
        ch.config().setWriteBufferWaterMark(new WriteBufferWaterMark(
                NettyClientSession.LOW_WATER_MARK,
                NettyClientSession.HIGH_WATER_MARK));

        ch.pipeline()
                .addLast(new HttpServerCodec())
                .addLast(new HttpObjectAggregator(65536))
                .addLast(new IdleStateHandler(0, 0, GatewayConfig.HEARTBEAT_INTERVAL_SEC, TimeUnit.SECONDS))
                .addLast(new WebSocketHandler(sessions, subscriptions, cache, dispatcher, aggregatePusher, wsFactory));
    }
}
