package com.quant.gateway.api;

import com.quant.gateway.aggregate.AggregatePusher;
import com.quant.gateway.aggregate.SlidingWindowAggregator;
import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.*;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.handler.codec.http.HttpObjectAggregator;
import io.netty.handler.codec.http.HttpServerCodec;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * 轻量 REST API 服务, 单独端口运行, 与 WebSocket 解耦。
 */
public final class RestApiServer {

    private static final Logger log = LoggerFactory.getLogger(RestApiServer.class);

    private final int port;
    private final SessionRegistry sessions;
    private final SubscriptionManager subscriptions;
    private final MarketCache cache;
    private final DefaultFeedDispatcher dispatcher;
    private final SlidingWindowAggregator aggregator;
    private final AggregatePusher aggregatePusher;

    private final NioEventLoopGroup boss = new NioEventLoopGroup(1);
    private final NioEventLoopGroup worker = new NioEventLoopGroup(2);
    private Channel channel;

    public RestApiServer(int port,
                         SessionRegistry sessions,
                         SubscriptionManager subscriptions,
                         MarketCache cache,
                         DefaultFeedDispatcher dispatcher,
                         SlidingWindowAggregator aggregator,
                         AggregatePusher aggregatePusher) {
        this.port = port;
        this.sessions = sessions;
        this.subscriptions = subscriptions;
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregator = aggregator;
        this.aggregatePusher = aggregatePusher;
    }

    public void start() throws InterruptedException {
        ServerBootstrap b = new ServerBootstrap()
                .group(boss, worker)
                .channel(NioServerSocketChannel.class)
                .childOption(ChannelOption.SO_KEEPALIVE, true)
                .childHandler(new ChannelInitializer<SocketChannel>() {
                    @Override
                    protected void initChannel(SocketChannel ch) {
                        ch.pipeline()
                                .addLast(new HttpServerCodec())
                                .addLast(new HttpObjectAggregator(65536))
                                .addLast(new RestApiHandler(sessions, subscriptions, cache, dispatcher, aggregator, aggregatePusher));
                    }
                });
        channel = b.bind(port).sync().channel();
        log.info("REST API listening on :{}", port);
    }

    public void stop() {
        boss.shutdownGracefully();
        worker.shutdownGracefully();
    }
}
