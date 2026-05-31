package com.quant.gateway.transport;

import com.quant.gateway.aggregate.SlidingWindowAggregator;
import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.codec.BinaryCodec;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;
import com.quant.gateway.config.GatewayConfig;
import com.quant.gateway.config.SectorMapping;
import io.netty.bootstrap.Bootstrap;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.*;
import io.netty.channel.nio.NioEventLoopGroup;
import io.netty.channel.socket.DatagramPacket;
import io.netty.channel.socket.nio.NioDatagramChannel;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.net.InetSocketAddress;
import java.util.Random;
import java.util.concurrent.atomic.AtomicLong;

/**
 * UDP 组播行情接收器 (同时提供内置模拟器, 便于本地验证)。
 *
 * 真实接入时监听 group:port 的组播流量, 将收到的数据包解析为 TickData
 * 并写入 MarketCache + 分发到 WebSocket 客户端。
 */
public final class UdpFeedReceiver {

    private static final Logger log = LoggerFactory.getLogger(UdpFeedReceiver.class);

    private final MarketCache cache;
    private final FeedDispatcher dispatcher;
    private final SlidingWindowAggregator aggregator;
    private final NioEventLoopGroup group;
    private Channel channel;

    public UdpFeedReceiver(MarketCache cache, FeedDispatcher dispatcher, SlidingWindowAggregator aggregator) {
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregator = aggregator;
        this.group = new NioEventLoopGroup(2);
    }

    public void start() throws Exception {
        Bootstrap b = new Bootstrap()
                .group(group)
                .channel(NioDatagramChannel.class)
                .option(ChannelOption.SO_RCVBUF, 4 * 1024 * 1024)
                .option(ChannelOption.SO_REUSEADDR, true)
                .handler(new ChannelInitializer<NioDatagramChannel>() {
                    @Override
                    protected void initChannel(NioDatagramChannel ch) {
                        ch.pipeline().addLast(new FeedDecoder());
                    }
                });
        try {
            channel = b.bind(GatewayConfig.UDP_PORT).sync().channel();
            log.info("UDP feed receiver bound :{} (group={})", GatewayConfig.UDP_PORT, GatewayConfig.UDP_GROUP);
        } catch (Exception e) {
            log.error("UDP bind failed, switch to simulator only", e);
        }
    }

    public void stop() {
        group.shutdownGracefully();
    }

    /**
     * 解析 UDP 数据包 -> TickData。
     * 数据包格式: [1 byte MsgType][payload bytes]
     * 为简化实现, 当 type=TICK/ORDER 时 payload 直接按 BinaryCodec.encodeTick 解析。
     */
    private final class FeedDecoder extends SimpleChannelInboundHandler<DatagramPacket> {
        @Override
        protected void channelRead0(ChannelHandlerContext ctx, DatagramPacket pkt) {
            ByteBuf buf = pkt.content();
            if (buf.readableBytes() < 2) return;
            int typeCode = buf.readByte();
            MsgType type = MsgType.fromCode(typeCode);
            if (type != MsgType.TICK && type != MsgType.ORDER) return;
            try {
                TickData tick = BinaryCodec.decodeTick(buf);
                tick.seq = cache.nextSeq();
                cache.put(tick);
                dispatcher.dispatch(tick);
                if (aggregator != null) aggregator.onTick(tick);
            } catch (Exception e) {
                log.warn("UDP decode error: {}", e.getMessage());
            }
        }
    }

    // ==================== 内置模拟行情源 ====================

    /**
     * 启动内置模拟器, 以 ~100k tick/s 的速率向本机 UDP 组播地址发送行情。
     */
    public static Thread startSimulator() {
        Thread t = new Thread(new SimRunner(), "feed-sim");
        t.setDaemon(true);
        t.start();
        return t;
    }

    private static final class SimRunner implements Runnable {
        @Override
        public void run() {
            NioEventLoopGroup g = new NioEventLoopGroup(1);
            try {
                Bootstrap b = new Bootstrap()
                        .group(g)
                        .channel(NioDatagramChannel.class)
                        .option(ChannelOption.SO_SNDBUF, 4 * 1024 * 1024);
                Channel ch = b.bind(0).sync().channel();
                InetSocketAddress target = new InetSocketAddress(GatewayConfig.UDP_GROUP, GatewayConfig.UDP_PORT);

                String[] symbols = SectorMapping.allSymbols().toArray(new String[0]);
                Random r = new Random(42);
                AtomicLong seq = new AtomicLong(0);
                double[] lastPrice = new double[symbols.length];
                for (int i = 0; i < symbols.length; i++) lastPrice[i] = 10 + r.nextInt(200);

                long periodNs = 10_000; // 100k/s
                long nextDeadline = System.nanoTime();
                while (!Thread.interrupted()) {
                    int idx = r.nextInt(symbols.length);
                    String symbol = symbols[idx];
                    double p = Math.max(0.1, lastPrice[idx] + (r.nextDouble() - 0.5) * 0.2);
                    lastPrice[idx] = p;
                    long v = 100L + r.nextInt(5000);
                    boolean tick = r.nextInt(10) < 7; // 70% 逐笔成交
                    TickData td;
                    if (tick) {
                        td = BinaryCodec.newTick(symbol, seq.incrementAndGet(), p, v,
                                r.nextBoolean() ? "B" : "S");
                    } else {
                        td = BinaryCodec.newOrder(symbol, seq.incrementAndGet(), p, v,
                                r.nextBoolean() ? "BID" : "ASK");
                    }
                    ByteBuf buf = Unpooled.buffer(128);
                    buf.writeByte(td.msgType.code());
                    ByteBuf encoded = BinaryCodec.encodeTick(td);
                    buf.writeBytes(encoded);
                    encoded.release();
                    ch.writeAndFlush(new DatagramPacket(buf, target));

                    nextDeadline += periodNs;
                    long sleep = nextDeadline - System.nanoTime();
                    if (sleep > 0) {
                        long ms = sleep / 1_000_000;
                        int ns = (int) (sleep % 1_000_000);
                        if (ms > 0) Thread.sleep(ms);
                        else Thread.sleep(0, ns);
                    } else if (sleep < -1_000_000_000) {
                        // 落后超过 1s, 重置
                        nextDeadline = System.nanoTime();
                    }
                }
            } catch (Exception e) {
                log.error("Simulator failed: {}", e.getMessage());
            } finally {
                g.shutdownGracefully();
            }
        }
    }
}
