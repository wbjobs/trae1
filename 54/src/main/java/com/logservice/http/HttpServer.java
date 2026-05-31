package com.logservice.http;

import com.logservice.config.ServiceConfig;
import com.logservice.storage.AggregationManager;
import com.logservice.storage.BlockManager;
import com.logservice.storage.IndexManager;
import com.logservice.storage.SearchService;
import io.netty.bootstrap.ServerBootstrap;
import io.netty.channel.*;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.SocketChannel;
import io.netty.channel.socket.nio.NioServerSocketChannel;
import io.netty.handler.codec.http.HttpObjectAggregator;
import io.netty.handler.codec.http.HttpServerCodec;
import io.netty.handler.logging.LogLevel;
import io.netty.handler.logging.LoggingHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class HttpServer {
    private static final Logger LOG = LoggerFactory.getLogger(HttpServer.class);

    private final ServiceConfig config;
    private final BlockManager blockManager;
    private final IndexManager indexManager;
    private final SearchService searchService;
    private final AggregationManager aggregationManager;

    private NioEventLoopGroup bossGroup;
    private NioEventLoopGroup workerGroup;
    private Channel serverChannel;

    public HttpServer(ServiceConfig config, BlockManager blockManager,
                       IndexManager indexManager, SearchService searchService,
                       AggregationManager aggregationManager) {
        this.config = config;
        this.blockManager = blockManager;
        this.indexManager = indexManager;
        this.searchService = searchService;
        this.aggregationManager = aggregationManager;
    }

    public void start() throws InterruptedException {
        int boss = Math.max(1, config.getHttpBossThreads());
        int worker = config.getHttpWorkerThreads();
        bossGroup = new NioEventLoopGroup(boss);
        workerGroup = worker <= 0 ? new NioEventLoopGroup() : new NioEventLoopGroup(worker);

        ServerBootstrap b = new ServerBootstrap();
        b.group(bossGroup, workerGroup)
                .channel(NioServerSocketChannel.class)
                .option(ChannelOption.SO_BACKLOG, 1024)
                .option(ChannelOption.SO_REUSEADDR, true)
                .childOption(ChannelOption.TCP_NODELAY, true)
                .childOption(ChannelOption.SO_KEEPALIVE, true)
                .childOption(ChannelOption.ALLOCATOR, io.netty.buffer.PooledByteBufAllocator.DEFAULT)
                .handler(new LoggingHandler(LogLevel.DEBUG))
                .childHandler(new ChannelInitializer<SocketChannel>() {
                    @Override
                    protected void initChannel(SocketChannel ch) {
                        ChannelPipeline p = ch.pipeline();
                        p.addLast(new HttpServerCodec(4096, 8192, 65536, false));
                        p.addLast(new HttpObjectAggregator(16 * 1024 * 1024));
                        p.addLast(new HttpApiHandler(config, blockManager, indexManager,
                                searchService, aggregationManager));
                    }
                });

        ChannelFuture f = b.bind(config.getHttpPort()).sync();
        serverChannel = f.channel();
        LOG.info("Log Compression Index Service HTTP server bound on port {}", config.getHttpPort());
    }

    public void shutdown() {
        LOG.info("Shutting down HTTP server");
        if (serverChannel != null) serverChannel.close().syncUninterruptibly();
        if (workerGroup != null) workerGroup.shutdownGracefully();
        if (bossGroup != null) bossGroup.shutdownGracefully();
    }
}
