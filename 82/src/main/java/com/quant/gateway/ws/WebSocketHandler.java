package com.quant.gateway.ws;

import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.codec.BinaryCodec;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.codec.TickData;
import com.quant.gateway.config.GatewayConfig;
import com.quant.gateway.aggregate.AggregatePusher;
import com.quant.gateway.session.ClientSession;
import com.quant.gateway.session.NettyClientSession;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import io.netty.channel.Channel;
import io.netty.channel.ChannelFutureListener;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import io.netty.handler.codec.http.*;
import io.netty.handler.codec.http.websocketx.*;
import io.netty.handler.timeout.IdleState;
import io.netty.handler.timeout.IdleStateEvent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;

import static io.netty.handler.codec.http.HttpHeaderNames.*;
import static io.netty.handler.codec.http.HttpResponseStatus.*;
import static io.netty.handler.codec.http.HttpVersion.*;

/**
 * WebSocket 消息处理器。
 *
 * 职责:
 *   - HTTP 升级到 WebSocket
 *   - 为新连接注册会话 + 配置背压监听器
 *   - 处理订阅请求 (二进制/文本)
 *   - 监听写缓冲区可写状态变化, 触发背压切换
 *   - 心跳检测
 *
 * 注: 不标记 @Sharable, 因为每个连接持有 NettyClientSession 引用作为 per-handler 状态。
 */
final class WebSocketHandler extends SimpleChannelInboundHandler<Object> {

    private static final Logger log = LoggerFactory.getLogger(WebSocketHandler.class);

    private final SessionRegistry sessions;
    private final SubscriptionManager subscriptions;
    private final MarketCache cache;
    private final DefaultFeedDispatcher dispatcher;
    private final AggregatePusher aggregatePusher;
    private final WebSocketServerHandshakerFactory wsFactory;

    private WebSocketServerHandshaker handshaker;
    private NettyClientSession currentSession;

    WebSocketHandler(SessionRegistry sessions,
                     SubscriptionManager subscriptions,
                     MarketCache cache,
                     DefaultFeedDispatcher dispatcher,
                     AggregatePusher aggregatePusher,
                     WebSocketServerHandshakerFactory wsFactory) {
        this.sessions = sessions;
        this.subscriptions = subscriptions;
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregatePusher = aggregatePusher;
        this.wsFactory = wsFactory;
    }

    @Override
    protected void channelRead0(ChannelHandlerContext ctx, Object msg) throws Exception {
        if (msg instanceof FullHttpRequest) {
            handleHttp(ctx, (FullHttpRequest) msg);
        } else if (msg instanceof WebSocketFrame) {
            handleWebSocket(ctx, (WebSocketFrame) msg);
        }
    }

    // ============ HTTP 升级 ============

    private void handleHttp(ChannelHandlerContext ctx, FullHttpRequest req) {
        if (!req.decoderResult().isSuccess()) {
            sendHttp(ctx, BAD_REQUEST);
            return;
        }
        if (!"websocket".equalsIgnoreCase(req.headers().get(UPGRADE))) {
            sendHttp(ctx, FORBIDDEN);
            return;
        }
        String clientId = parseClientId(req);
        if (clientId == null || clientId.isEmpty()) {
            sendHttp(ctx, BAD_REQUEST, "missing clientId");
            return;
        }
        long lastSeq = sessions.offlineAckSeq(clientId);

        WebSocketServerHandshaker hs = wsFactory.newHandshaker(req);
        if (hs == null) {
            WebSocketServerHandshakerFactory.sendUnsupportedVersionResponse(ctx.channel());
            return;
        }
        handshaker = hs;
        hs.handshake(ctx.channel(), req).addListener(f -> {
            if (f.isSuccess()) {
                registerSession(ctx.channel(), clientId, lastSeq);
            }
        });
    }

    private String parseClientId(FullHttpRequest req) {
        QueryStringDecoder q = new QueryStringDecoder(req.uri());
        List<String> ids = q.parameters().get("clientId");
        if (ids != null && !ids.isEmpty()) return ids.get(0);
        return req.headers().get("X-Client-Id");
    }

    private void registerSession(Channel ch, String clientId, long lastSeq) {
        NettyClientSession session = new NettyClientSession(clientId, ch);

        // 绑定背压监听器: 背压事件传播到上游分发器
        if (dispatcher instanceof DefaultFeedDispatcher) {
            session.setBackPressureListener((DefaultFeedDispatcher) dispatcher);
        }

        ClientSession prev = sessions.online(clientId, session);
        if (prev == null) {
            log.warn("MAX_CLIENTS reached, reject {}", clientId);
            session.close();
            return;
        }
        this.currentSession = session;
        ch.attr(Attrs.CLIENT_ID).set(clientId);
        log.info("client connected: {} lastSeq={} remote={}", clientId, lastSeq, ch.remoteAddress());

        // 重连补发
        if (lastSeq > 0) {
            SubscriptionManager.SubView view = subscriptions.viewOf(clientId);
            if (view != null) {
                boolean any = false;
                for (String symbol : view.symbols) {
                    List<TickData> ticks = cache.replayBySymbol(symbol, lastSeq + 1);
                    if (!ticks.isEmpty()) {
                        session.pushReplay(ticks);
                        any = true;
                    }
                }
                for (String sector : view.sectors) {
                    List<TickData> ticks = cache.replayBySector(sector, lastSeq + 1);
                    if (!ticks.isEmpty()) {
                        session.pushReplay(ticks);
                        any = true;
                    }
                }
                if (any) log.info("replay to {} ({} ticks)", clientId, ticksCount(view, cache));
            }
            sessions.clearOffline(clientId);
        }

        // 握手 ack
        byte[] ack = ("hello " + clientId).getBytes();
        session.pushControl(MsgType.HELLO, ack);

        // 全局降级通知
        if (cache.isDegraded()) {
            session.pushControl(MsgType.DEGRADE_NOTICE, "FEED_DOWN".getBytes());
        }
    }

    private static int ticksCount(SubscriptionManager.SubView view, MarketCache cache) {
        return view.symbols.size() + view.sectors.size();
    }

    // ============ WebSocket 帧 ============

    private void handleWebSocket(ChannelHandlerContext ctx, WebSocketFrame frame) {
        if (frame instanceof CloseWebSocketFrame) {
            handshaker.close(ctx.channel(), (CloseWebSocketFrame) frame.retain());
            return;
        }
        if (frame instanceof PingWebSocketFrame) {
            ctx.writeAndFlush(new PongWebSocketFrame(frame.content().retain()));
            return;
        }
        if (frame instanceof BinaryWebSocketFrame) {
            handleBinary(ctx, (BinaryWebSocketFrame) frame);
            return;
        }
        if (frame instanceof TextWebSocketFrame) {
            String text = ((TextWebSocketFrame) frame).text();
            handleText(ctx, text);
        }
    }

    private void handleBinary(ChannelHandlerContext ctx, BinaryWebSocketFrame frame) {
        ByteBuf buf = frame.content();
        try {
            BinaryCodec.Frame f = BinaryCodec.unwrapFrame(buf);
            String clientId = ctx.channel().attr(Attrs.CLIENT_ID).get();
            if (clientId == null) return;
            ClientSession session = sessions.findOnline(clientId);
            if (session == null) return;
            switch (f.type) {
                case SUBSCRIBE_REQ:
                    handleSubscribeReq(ctx, session, f.payload);
                    break;
                case HEARTBEAT:
                    session.pushControl(MsgType.HEARTBEAT, new byte[0]);
                    break;
                default:
                    log.debug("unsupported msg type: {}", f.type);
            }
        } catch (Exception e) {
            log.debug("binary decode err: {}", e.getMessage());
        }
    }

    private void handleSubscribeReq(ChannelHandlerContext ctx, ClientSession session, ByteBuf payload) {
        BinaryCodec.SubscribeReq req = BinaryCodec.decodeSubscribeReq(payload);
        if (req.clientId == null) req.clientId = session.getClientId();
        boolean cancel = req.subType == 3;
        switch (req.subType) {
            case 0:
                for (String s : req.items) {
                    if (cancel) subscriptions.unsubscribeSymbol(req.clientId, session, s);
                    else        subscriptions.subscribeSymbol(req.clientId, session, s);
                }
                break;
            case 1:
                for (String s : req.items) {
                    if (cancel) subscriptions.unsubscribeSector(req.clientId, session, s);
                    else        subscriptions.subscribeSector(req.clientId, session, s);
                }
                break;
            case 2:
                for (int bit = 0; bit < 3; bit++) {
                    if ((req.kindsMask & (1 << bit)) != 0) {
                        MsgType kind = bit == 0 ? MsgType.TICK : (bit == 1 ? MsgType.ORDER : MsgType.SNAPSHOT);
                        if (cancel) subscriptions.unsubscribeKind(req.clientId, session, kind);
                        else        subscriptions.subscribeKind(req.clientId, session, kind);
                    }
                }
                break;
            default:
                break;
        }
        byte[] body = ("ok subType=" + req.subType + " cancel=" + cancel).getBytes();
        session.pushControl(MsgType.SUBSCRIBE_RESP, body);
    }

    private void handleText(ChannelHandlerContext ctx, String text) {
        String clientId = ctx.channel().attr(Attrs.CLIENT_ID).get();
        if (clientId == null) return;
        ClientSession session = sessions.findOnline(clientId);
        if (session == null) return;
        String[] parts = text.trim().split("\\s+");
        if (parts.length < 2) return;
        switch (parts[0]) {
            case "sub":
                for (int i = 1; i < parts.length; i++) subscriptions.subscribeSymbol(clientId, session, parts[i]);
                session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok sub " + text).getBytes());
                break;
            case "unsub":
                for (int i = 1; i < parts.length; i++) subscriptions.unsubscribeSymbol(clientId, session, parts[i]);
                session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok unsub " + text).getBytes());
                break;
            case "sub-sector":
                for (int i = 1; i < parts.length; i++) subscriptions.subscribeSector(clientId, session, parts[i]);
                session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok sector " + text).getBytes());
                break;
            case "sub-kind":
                for (int i = 1; i < parts.length; i++) {
                    if (parts[i].equalsIgnoreCase("tick"))  subscriptions.subscribeKind(clientId, session, MsgType.TICK);
                    if (parts[i].equalsIgnoreCase("order")) subscriptions.subscribeKind(clientId, session, MsgType.ORDER);
                }
                session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok kind " + text).getBytes());
                break;
            case "sub-agg":
                if (aggregatePusher != null) {
                    for (int i = 1; i < parts.length; i++) aggregatePusher.subscribe(clientId, parts[i]);
                    session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok agg " + text).getBytes());
                } else {
                    session.pushControl(MsgType.SUBSCRIBE_RESP, ("aggregator disabled").getBytes());
                }
                break;
            case "unsub-agg":
                if (aggregatePusher != null) {
                    for (int i = 1; i < parts.length; i++) aggregatePusher.unsubscribe(clientId, parts[i]);
                    session.pushControl(MsgType.SUBSCRIBE_RESP, ("ok unagg " + text).getBytes());
                }
                break;
            default:
                break;
        }
    }

    // ============ 写缓冲区可写状态变化 (背压触发点) ============

    /**
     * 当写缓冲区越过高低水位时, Netty 自动调用此方法。
     *   - isWritable=true:  缓冲区从 HIGH 降到 LOW 以下, 解除背压
     *   - isWritable=false: 缓冲区从 LOW 升到 HIGH 以上, 进入背压
     */
    @Override
    public void channelWritabilityChanged(ChannelHandlerContext ctx) throws Exception {
        boolean writable = ctx.channel().isWritable();
        if (currentSession != null) {
            currentSession.onWritabilityChanged(writable);
        }
        super.channelWritabilityChanged(ctx);
    }

    // ============ 生命周期 ============

    @Override
    public void channelInactive(ChannelHandlerContext ctx) {
        String clientId = ctx.channel().attr(Attrs.CLIENT_ID).get();
        if (clientId != null) {
            ClientSession s = sessions.findOnline(clientId);
            long seq = s != null ? s.lastAckSeq() : 0;
            sessions.offline(clientId, seq);
            if (s != null) subscriptions.clearSession(s);
            if (aggregatePusher != null) aggregatePusher.onClientDisconnect(clientId);
            log.info("client disconnected: {}", clientId);
        }
        currentSession = null;
    }

    @Override
    public void userEventTriggered(ChannelHandlerContext ctx, Object evt) throws Exception {
        if (evt instanceof IdleStateEvent) {
            if (((IdleStateEvent) evt).state() == IdleState.ALL_IDLE) {
                String clientId = ctx.channel().attr(Attrs.CLIENT_ID).get();
                ClientSession s = clientId == null ? null : sessions.findOnline(clientId);
                if (s != null) {
                    s.pushControl(MsgType.HEARTBEAT, new byte[0]);
                    if (cache.isDegraded()) {
                        s.pushControl(MsgType.DEGRADE_NOTICE, "FEED_DOWN".getBytes());
                    }
                }
            }
            return;
        }
        super.userEventTriggered(ctx, evt);
    }

    @Override
    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
        log.debug("ws err: {}", cause.getMessage());
        ctx.close();
    }

    private static void sendHttp(ChannelHandlerContext ctx, HttpResponseStatus status) {
        sendHttp(ctx, status, "");
    }

    private static void sendHttp(ChannelHandlerContext ctx, HttpResponseStatus status, String body) {
        FullHttpResponse res = new DefaultFullHttpResponse(HTTP_1_1, status,
                Unpooled.copiedBuffer(body == null ? "" : body, io.netty.util.CharsetUtil.UTF_8));
        res.headers().set(CONTENT_TYPE, "text/plain; charset=utf-8");
        ctx.writeAndFlush(res).addListener(ChannelFutureListener.CLOSE);
    }
}
