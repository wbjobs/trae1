package com.quant.gateway.ws;

import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import com.quant.gateway.aggregate.AggregatePusher;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.*;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * WebSocket 行情推送服务。
 *
 * - 最多 1000 并发连接, 每个连接 64KB 高低水位缓冲
 * - 连接数满时拒绝新连接
 */
public final class WebSocketServer {

    private static final Logger log = LoggerFactory.getLogger(WebSocketServer.class);

    private final String host;
    private final int port;

    private final NioEventLoopGroup boss = new NioEventLoopGroup(1);
    private final NioEventLoopGroup worker = new NioEventLoopGroup();

    private final SessionRegistry sessions;
    private final SubscriptionManager subscriptions;
    private final MarketCache cache;
    private final DefaultFeedDispatcher dispatcher;
    private final AggregatePusher aggregatePusher;

    private Channel channel;

    public WebSocketServer(String host, int port,
                           SessionRegistry sessions,
                           SubscriptionManager subscriptions,
                           MarketCache cache,
                           DefaultFeedDispatcher dispatcher,
                           AggregatePusher aggregatePusher) {
        this.host = host;
        this.port = port;
        this.sessions = sessions;
        this.subscriptions = subscriptions;
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregatePusher = aggregatePusher;
    }

    public void start() throws InterruptedException {
        ServerBootstrap b = new ServerBootstrap()
                .group(boss, worker)
                .channel(NioServerSocketChannel.class)
                .option(ChannelOption.SO_BACKLOG, 1024)
                .childOption(ChannelOption.SO_KEEPALIVE, true)
                .childOption(ChannelOption.TCP_NODELAY, true)
                .childHandler(new WebSocketServerInitializer(sessions, subscriptions, cache, dispatcher, aggregatePusher));
        channel = b.bind(host, port).sync().channel();
        log.info("WebSocket listening on {}:{}", host, port);
    }

    public void stop() {
        boss.shutdownGracefully();
        worker.shutdownGracefully();
    }
}
