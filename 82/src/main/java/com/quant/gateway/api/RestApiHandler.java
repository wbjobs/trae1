package com.quant.gateway.api;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.quant.gateway.aggregate.AggregatePusher;
import com.quant.gateway.aggregate.AggregateResult;
import com.quant.gateway.aggregate.AggregateRule;
import com.quant.gateway.aggregate.SlidingWindowAggregator;
import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.codec.MsgType;
import com.quant.gateway.session.ClientSession;
import com.quant.gateway.session.NettyClientSession;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import io.netty.buffer.Unpooled;
import io.netty.channel.ChannelFutureListener;
import io.netty.channel.ChannelHandlerContext;
import io.netty.channel.SimpleChannelInboundHandler;
import io.netty.handler.codec.http.*;
import io.netty.util.CharsetUtil;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;

import static io.netty.handler.codec.http.HttpHeaderNames.*;
import static io.netty.handler.codec.http.HttpResponseStatus.*;
import static io.netty.handler.codec.http.HttpVersion.*;

/**
 * REST API 处理器。
 *
 * 端点:
 *   GET /api/clients              -> 在线客户端列表 (含缓冲区状态)
 *   GET /api/clients/{id}/subs    -> 指定客户端订阅列表
 *   GET /api/clients/{id}/buffer  -> 指定客户端缓冲区占用详情
 *   GET /api/buffer-status        -> 所有连接缓冲区占用情况
 *   GET /api/backpressure         -> 背压状态 (暂停的客户端)
 *   GET /api/stats                -> 网关运行指标
 *   GET /api/cache/{symbol}       -> 指定 symbol 最近一笔行情
 */
public final class RestApiHandler extends SimpleChannelInboundHandler<FullHttpRequest> {

    private static final Logger log = LoggerFactory.getLogger(RestApiHandler.class);

    private final ObjectMapper mapper = new ObjectMapper();
    private final SessionRegistry sessions;
    private final SubscriptionManager subscriptions;
    private final MarketCache cache;
    private final DefaultFeedDispatcher dispatcher;
    private final SlidingWindowAggregator aggregator;
    private final AggregatePusher aggregatePusher;

    public RestApiHandler(SessionRegistry sessions,
                          SubscriptionManager subscriptions,
                          MarketCache cache,
                          DefaultFeedDispatcher dispatcher,
                          SlidingWindowAggregator aggregator,
                          AggregatePusher aggregatePusher) {
        this.sessions = sessions;
        this.subscriptions = subscriptions;
        this.cache = cache;
        this.dispatcher = dispatcher;
        this.aggregator = aggregator;
        this.aggregatePusher = aggregatePusher;
    }

    @Override
    protected void channelRead0(ChannelHandlerContext ctx, FullHttpRequest req) {
        QueryStringDecoder q = new QueryStringDecoder(req.uri());
        String path = q.path();
        try {
            if ("/api/clients".equals(path)) {
                json(ctx, clientsJson());
            } else if (path.startsWith("/api/clients/") && path.endsWith("/subs")) {
                String id = path.substring("/api/clients/".length(), path.length() - "/subs".length());
                json(ctx, subsJson(id));
            } else if (path.startsWith("/api/clients/") && path.endsWith("/buffer")) {
                String id = path.substring("/api/clients/".length(), path.length() - "/buffer".length());
                json(ctx, clientBufferJson(id));
            } else if ("/api/buffer-status".equals(path)) {
                json(ctx, bufferStatusJson());
            } else if ("/api/backpressure".equals(path)) {
                json(ctx, backpressureJson());
            } else if ("/api/stats".equals(path)) {
                json(ctx, statsJson());
            } else if (path.startsWith("/api/cache/")) {
                String symbol = path.substring("/api/cache/".length());
                json(ctx, cacheJson(symbol));
            }
            // ========== 聚合规则 API ==========
            else if ("/api/rules".equals(path)) {
                if (req.method() == HttpMethod.GET) {
                    json(ctx, listRulesJson());
                } else if (req.method() == HttpMethod.POST) {
                    json(ctx, createRuleJson(req));
                } else {
                    json(ctx, mapper.createObjectNode().put("error", "method not allowed"), METHOD_NOT_ALLOWED);
                }
            } else if (path.startsWith("/api/rules/")) {
                String ruleId = path.substring("/api/rules/".length());
                if (req.method() == HttpMethod.GET) {
                    json(ctx, getRuleJson(ruleId));
                } else if (req.method() == HttpMethod.DELETE) {
                    json(ctx, deleteRuleJson(ruleId));
                } else if (req.method() == HttpMethod.PUT) {
                    json(ctx, toggleRuleJson(ruleId, req));
                } else {
                    json(ctx, mapper.createObjectNode().put("error", "method not allowed"), METHOD_NOT_ALLOWED);
                }
            } else if (path.startsWith("/api/rules/") && path.endsWith("/snapshot")) {
                String ruleId = path.substring("/api/rules/".length(), path.length() - "/snapshot".length());
                json(ctx, ruleSnapshotJson(ruleId));
            } else if ("/api/aggregate-stats".equals(path)) {
                json(ctx, aggregateStatsJson());
            }
            // =====================================
            else {
                json(ctx, mapper.createObjectNode().put("error", "not found"), NOT_FOUND);
            }
        } catch (Exception e) {
            log.warn("api err: {}", e.getMessage());
            json(ctx, mapper.createObjectNode().put("error", e.getMessage()), INTERNAL_SERVER_ERROR);
        }
    }

    // ============ /api/clients ============

    private ObjectNode clientsJson() {
        ObjectNode root = mapper.createObjectNode();
        ArrayNode arr = root.putArray("clients");
        for (Map.Entry<String, ClientSession> e : sessions.allOnline().entrySet()) {
            ObjectNode n = mapper.createObjectNode();
            ClientSession s = e.getValue();
            n.put("clientId", e.getKey());
            n.put("remote", s.remoteAddress());
            n.put("connectTime", s.connectTime());
            n.put("lastActivity", s.lastActivity());
            n.put("lastAckSeq", s.lastAckSeq());
            // 缓冲区状态
            if (s instanceof NettyClientSession) {
                NettyClientSession ns = (NettyClientSession) s;
                ObjectNode buf = n.putObject("buffer");
                buf.put("pendingBytes", ns.pendingBytes());
                buf.put("highWaterMark", NettyClientSession.HIGH_WATER_MARK);
                buf.put("lowWaterMark", NettyClientSession.LOW_WATER_MARK);
                buf.put("usagePercent", round2(ns.bufferUsagePercent()));
                buf.put("isBackPressured", ns.isBackPressured());
                buf.put("backPressureDurationMs", ns.backPressureDurationMs());
                buf.put("backPressureEvents", ns.backPressureEvents());
                buf.put("dropped", ns.droppedCount());
                buf.put("aggregateQueueSize", ns.aggregateQueueSize());
            }
            // 上游背压状态
            if (dispatcher != null) {
                n.put("upstreamPaused", dispatcher.isPaused(e.getKey()));
            }
            arr.add(n);
        }
        root.put("online", sessions.onlineCount());
        root.put("offlineCached", sessions.offlineCount());
        return root;
    }

    // ============ /api/clients/{id}/buffer ============

    private ObjectNode clientBufferJson(String clientId) {
        ObjectNode root = mapper.createObjectNode();
        root.put("clientId", clientId);
        ClientSession s = sessions.findOnline(clientId);
        if (s == null) {
            root.put("found", false);
            root.put("message", "client not online");
            return root;
        }
        if (s instanceof NettyClientSession) {
            NettyClientSession ns = (NettyClientSession) s;
            root.put("found", true);
            root.put("pendingBytes", ns.pendingBytes());
            root.put("highWaterMark", NettyClientSession.HIGH_WATER_MARK);
            root.put("lowWaterMark", NettyClientSession.LOW_WATER_MARK);
            root.put("usagePercent", round2(ns.bufferUsagePercent()));
            root.put("isBackPressured", ns.isBackPressured());
            root.put("backPressureDurationMs", ns.backPressureDurationMs());
            root.put("backPressureEvents", ns.backPressureEvents());
            root.put("droppedCount", ns.droppedCount());
            root.put("aggregateQueueSize", ns.aggregateQueueSize());
            root.put("lastAckSeq", ns.lastAckSeq());
            root.put("remote", ns.remoteAddress());
            root.put("connectTime", ns.connectTime());
            if (dispatcher != null) {
                root.put("upstreamPaused", dispatcher.isPaused(clientId));
                root.put("upstreamSkippedTicks", dispatcher.skippedCount(clientId));
            }
        } else {
            root.put("found", false);
            root.put("message", "session not a NettyClientSession");
        }
        return root;
    }

    // ============ /api/buffer-status ============

    private ObjectNode bufferStatusJson() {
        ObjectNode root = mapper.createObjectNode();
        root.put("generatedAt", System.currentTimeMillis());
        root.put("highWaterMark", NettyClientSession.HIGH_WATER_MARK);
        root.put("lowWaterMark", NettyClientSession.LOW_WATER_MARK);

        ArrayNode clients = root.putArray("clients");
        int totalPending = 0;
        int backPressured = 0;
        int maxUsage = 0;
        String maxUsageClient = "-";
        long totalDropped = 0;

        for (Map.Entry<String, ClientSession> e : sessions.allOnline().entrySet()) {
            ClientSession s = e.getValue();
            if (!(s instanceof NettyClientSession)) continue;
            NettyClientSession ns = (NettyClientSession) s;

            ObjectNode n = mapper.createObjectNode();
            n.put("clientId", e.getKey());
            int pending = ns.pendingBytes();
            n.put("pendingBytes", pending);
            double usage = ns.bufferUsagePercent();
            n.put("usagePercent", round2(usage));
            n.put("backPressured", ns.isBackPressured());
            n.put("dropped", ns.droppedCount());
            n.put("aggregateQueue", ns.aggregateQueueSize());
            if (dispatcher != null) {
                n.put("upstreamPaused", dispatcher.isPaused(e.getKey()));
            }
            clients.add(n);

            totalPending += pending;
            if (ns.isBackPressured()) backPressured++;
            if (usage > maxUsage) {
                maxUsage = (int) usage;
                maxUsageClient = e.getKey();
            }
            totalDropped += ns.droppedCount();
        }

        root.put("totalClients", clients.size());
        root.put("totalPendingBytes", totalPending);
        root.put("backPressuredCount", backPressured);
        root.put("totalDropped", totalDropped);
        root.put("maxUsageClient", maxUsageClient);
        root.put("maxUsagePercent", maxUsage);

        if (dispatcher != null) {
            ObjectNode bp = root.putObject("upstream");
            bp.put("pausedCount", dispatcher.pausedCount());
            ArrayNode paused = bp.putArray("pausedClients");
            for (String pid : dispatcher.pausedClientIds()) paused.add(pid);
        }
        return root;
    }

    // ============ /api/backpressure ============

    private ObjectNode backpressureJson() {
        ObjectNode root = mapper.createObjectNode();
        root.put("generatedAt", System.currentTimeMillis());

        ArrayNode backPressuredClients = root.putArray("backPressuredClients");
        int totalBackPressured = 0;
        long totalBackPressureDuration = 0;

        for (Map.Entry<String, ClientSession> e : sessions.allOnline().entrySet()) {
            ClientSession s = e.getValue();
            if (!(s instanceof NettyClientSession)) continue;
            NettyClientSession ns = (NettyClientSession) s;
            if (ns.isBackPressured()) {
                ObjectNode n = mapper.createObjectNode();
                n.put("clientId", e.getKey());
                n.put("pendingBytes", ns.pendingBytes());
                n.put("usagePercent", round2(ns.bufferUsagePercent()));
                n.put("durationMs", ns.backPressureDurationMs());
                n.put("events", ns.backPressureEvents());
                n.put("dropped", ns.droppedCount());
                n.put("aggregateQueue", ns.aggregateQueueSize());
                if (dispatcher != null) {
                    n.put("upstreamPaused", dispatcher.isPaused(e.getKey()));
                    n.put("upstreamSkippedTicks", dispatcher.skippedCount(e.getKey()));
                }
                backPressuredClients.add(n);
                totalBackPressured++;
                totalBackPressureDuration += ns.backPressureDurationMs();
            }
        }

        root.put("backPressuredCount", totalBackPressured);
        root.put("avgDurationMs", totalBackPressured > 0 ? totalBackPressureDuration / totalBackPressured : 0);
        if (dispatcher != null) {
            ObjectNode up = root.putObject("upstream");
            up.put("pausedCount", dispatcher.pausedCount());
            ArrayNode pausedIds = up.putArray("pausedClientIds");
            for (String pid : dispatcher.pausedClientIds()) pausedIds.add(pid);
        }
        return root;
    }

    // ============ /api/clients/{id}/subs ============

    private ObjectNode subsJson(String clientId) {
        ObjectNode root = mapper.createObjectNode();
        root.put("clientId", clientId);
        SubscriptionManager.SubView v = subscriptions.viewOf(clientId);
        if (v == null) {
            root.put("found", false);
            return root;
        }
        ArrayNode syms = root.putArray("symbols");
        for (String s : v.symbols) syms.add(s);
        ArrayNode secs = root.putArray("sectors");
        for (String s : v.sectors) secs.add(s);
        ArrayNode kinds = root.putArray("kinds");
        for (MsgType k : v.kinds) kinds.add(k.name());
        root.put("found", true);
        return root;
    }

    // ============ /api/stats ============

    private ObjectNode statsJson() {
        ObjectNode root = mapper.createObjectNode();
        root.put("onlineClients", sessions.onlineCount());
        root.put("offlineCached", sessions.offlineCount());
        root.put("totalSubscriptions", subscriptions.totalSubscribers());
        root.put("degraded", cache.isDegraded());
        root.put("feedAlive", cache.isFeedAlive());

        // 缓冲区统计
        int totalPending = 0;
        int backPressured = 0;
        long totalDropped = 0;
        for (ClientSession s : sessions.allOnline().values()) {
            if (s instanceof NettyClientSession) {
                NettyClientSession ns = (NettyClientSession) s;
                totalPending += ns.pendingBytes();
                if (ns.isBackPressured()) backPressured++;
                totalDropped += ns.droppedCount();
            }
        }
        ObjectNode buf = root.putObject("buffer");
        buf.put("totalPendingBytes", totalPending);
        buf.put("backPressuredCount", backPressured);
        buf.put("totalDropped", totalDropped);

        if (dispatcher != null) {
            ObjectNode bp = root.putObject("backpressure");
            bp.put("upstreamPausedCount", dispatcher.pausedCount());
        }

        ObjectNode sizes = root.putObject("cache");
        for (Map.Entry<String, Integer> e : cache.cacheStats().entrySet()) {
            sizes.put(e.getKey(), e.getValue());
        }
        return root;
    }

    // ============ /api/cache/{symbol} ============

    private ObjectNode cacheJson(String symbol) {
        ObjectNode root = mapper.createObjectNode();
        com.quant.gateway.codec.TickData t = cache.last(symbol);
        if (t == null) {
            root.put("found", false);
        } else {
            root.put("found", true);
            root.put("symbol", t.symbol);
            root.put("sector", t.sector);
            root.put("msgType", t.msgType.name());
            root.put("timestamp", t.timestamp);
            root.put("seq", t.seq);
            root.put("price", t.price);
            root.put("volume", t.volume);
            root.put("turnover", t.turnover);
        }
        return root;
    }

    // ============ helper ============

    private static double round2(double v) {
        return Math.round(v * 100.0) / 100.0;
    }

    // ============ 聚合规则 API ============

    /** GET /api/rules - 列出所有规则 */
    private ObjectNode listRulesJson() {
        ObjectNode root = mapper.createObjectNode();
        ArrayNode arr = root.putArray("rules");
        if (aggregator == null) {
            root.put("enabled", false);
            return root;
        }
        root.put("enabled", true);
        for (AggregateRule rule : aggregator.getAllRules()) {
            arr.add(ruleToJson(rule));
        }
        root.put("count", arr.size());
        return root;
    }

    /** GET /api/rules/{id} - 获取单个规则 */
    private ObjectNode getRuleJson(String ruleId) {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("found", false);
            return root;
        }
        AggregateRule rule = aggregator.getRule(ruleId);
        if (rule == null) {
            root.put("found", false);
            return root;
        }
        root.put("found", true);
        root.set("rule", ruleToJson(rule));
        if (aggregatePusher != null) {
            root.put("subscriberCount", aggregatePusher.subscriberCount(ruleId));
        }
        return root;
    }

    /** POST /api/rules - 创建规则 */
    private ObjectNode createRuleJson(FullHttpRequest req) {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("success", false);
            root.put("error", "aggregator not enabled");
            return root;
        }
        try {
            String body = req.content().toString(CharsetUtil.UTF_8);
            java.util.Map<String, Object> params = mapper.readValue(body, java.util.Map.class);

            String id = (String) params.get("id");
            String dsl = (String) params.get("dsl");
            if (id == null || dsl == null) {
                root.put("success", false);
                root.put("error", "id and dsl are required");
                return root;
            }

            AggregateRule.Builder b = AggregateRule.builder().id(id).dsl(dsl);
            if (params.get("name") != null) b.name((String) params.get("name"));
            if (params.get("windowSizeMs") != null) b.windowSizeMs(((Number) params.get("windowSizeMs")).longValue());
            if (params.get("pushIntervalMs") != null) b.pushIntervalMs(((Number) params.get("pushIntervalMs")).longValue());
            if (params.get("bigOrderThreshold") != null) b.bigOrderThreshold(((Number) params.get("bigOrderThreshold")).longValue());
            if (params.get("deltaEnabled") != null) b.deltaEnabled((Boolean) params.get("deltaEnabled"));
            if (params.get("symbols") instanceof java.util.Collection) {
                @SuppressWarnings("unchecked")
                java.util.Collection<String> syms = (java.util.Collection<String>) params.get("symbols");
                b.symbols(syms);
            }
            if (params.get("sectors") instanceof java.util.Collection) {
                @SuppressWarnings("unchecked")
                java.util.Collection<String> secs = (java.util.Collection<String>) params.get("sectors");
                b.sectors(secs);
            }

            AggregateRule rule = b.build();
            aggregator.registerRule(rule);
            root.put("success", true);
            root.put("ruleId", rule.getId());
        } catch (Exception e) {
            root.put("success", false);
            root.put("error", e.getMessage());
        }
        return root;
    }

    /** DELETE /api/rules/{id} - 删除规则 */
    private ObjectNode deleteRuleJson(String ruleId) {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("success", false);
            root.put("error", "aggregator not enabled");
            return root;
        }
        boolean removed = aggregator.removeRule(ruleId);
        root.put("success", removed);
        root.put("ruleId", ruleId);
        return root;
    }

    /** PUT /api/rules/{id} - 启用/禁用规则 */
    private ObjectNode toggleRuleJson(String ruleId, FullHttpRequest req) {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("success", false);
            return root;
        }
        AggregateRule rule = aggregator.getRule(ruleId);
        if (rule == null) {
            root.put("success", false);
            root.put("error", "rule not found");
            return root;
        }
        try {
            String body = req.content().toString(CharsetUtil.UTF_8);
            java.util.Map<String, Object> params = body.isEmpty() ? java.util.Collections.emptyMap()
                    : mapper.readValue(body, java.util.Map.class);
            if (params.get("enabled") != null) {
                boolean enabled = (Boolean) params.get("enabled");
                rule.setEnabled(enabled);
                root.put("success", true);
                root.put("enabled", enabled);
            } else {
                root.put("success", false);
                root.put("error", "'enabled' field required");
            }
        } catch (Exception e) {
            root.put("success", false);
            root.put("error", e.getMessage());
        }
        return root;
    }

    /** GET /api/rules/{id}/snapshot - 获取规则当前快照 */
    private ObjectNode ruleSnapshotJson(String ruleId) {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("found", false);
            return root;
        }
        java.util.Map<String, AggregateResult> snapshots = aggregator.snapshotAll(ruleId);
        root.put("ruleId", ruleId);
        root.put("found", !snapshots.isEmpty());
        ArrayNode arr = root.putArray("results");
        for (java.util.Map.Entry<String, AggregateResult> e : snapshots.entrySet()) {
            arr.add(resultToJson(e.getValue()));
        }
        return root;
    }

    /** GET /api/aggregate-stats - 聚合引擎运行统计 */
    private ObjectNode aggregateStatsJson() {
        ObjectNode root = mapper.createObjectNode();
        if (aggregator == null) {
            root.put("enabled", false);
            return root;
        }
        root.put("enabled", true);
        root.put("ruleCount", aggregator.ruleCount());
        root.put("activeWindowCount", aggregator.getActiveWindowCount());
        root.put("totalTicks", aggregator.getTotalTicks());
        root.put("totalAggregates", aggregator.getTotalAggregates());
        root.put("maxLatencyNs", aggregator.getMaxLatencyNs());
        root.put("maxLatencyMs", aggregator.getMaxLatencyNs() / 1_000_000.0);
        if (aggregatePusher != null) {
            ObjectNode subs = root.putObject("subscriptions");
            for (java.util.Map.Entry<String, java.util.Set<String>> e : aggregatePusher.allSubscriptions().entrySet()) {
                subs.put(e.getKey(), e.getValue().size());
            }
        }
        return root;
    }

    private ObjectNode ruleToJson(AggregateRule rule) {
        ObjectNode n = mapper.createObjectNode();
        n.put("id", rule.getId());
        n.put("name", rule.getName());
        n.put("dsl", rule.getDsl());
        n.put("windowSizeMs", rule.getWindowSizeMs());
        n.put("pushIntervalMs", rule.getPushIntervalMs());
        n.put("bigOrderThreshold", rule.getBigOrderThreshold());
        n.put("deltaEnabled", rule.isDeltaEnabled());
        n.put("enabled", rule.isEnabled());
        ArrayNode syms = n.putArray("symbols");
        for (String s : rule.getSymbols()) syms.add(s);
        ArrayNode secs = n.putArray("sectors");
        for (String s : rule.getSectors()) secs.add(s);
        n.put("ticksProcessed", rule.getTotalTicksProcessed());
        n.put("aggregatesGenerated", rule.getTotalAggregatesGenerated());
        if (aggregatePusher != null) {
            n.put("subscriberCount", aggregatePusher.subscriberCount(rule.getId()));
        }
        return n;
    }

    private ObjectNode resultToJson(AggregateResult r) {
        ObjectNode n = mapper.createObjectNode();
        n.put("ruleId", r.ruleId);
        n.put("symbol", r.symbol);
        n.put("sector", r.sector);
        n.put("windowStart", r.windowStart);
        n.put("windowEnd", r.windowEnd);
        n.put("count", r.count);
        n.put("sumVolume", r.sumVolume);
        n.put("sumTurnover", r.sumTurnover);
        n.put("avgPrice", r.avgPrice);
        n.put("wavgPrice", r.wavgPrice);
        n.put("high", r.high);
        n.put("low", r.low);
        n.put("open", r.open);
        n.put("close", r.close);
        n.put("bigOrderCount", r.bigOrderCount);
        n.put("bigOrderVolume", r.bigOrderVolume);
        ObjectNode mets = n.putObject("customMetrics");
        for (java.util.Map.Entry<String, Double> e : r.customMetrics.entrySet()) {
            mets.put(e.getKey(), e.getValue());
        }
        return n;
    }

    private void json(ChannelHandlerContext ctx, ObjectNode node) {
        json(ctx, node, OK);
    }

    private void json(ChannelHandlerContext ctx, ObjectNode node, HttpResponseStatus status) {
        byte[] bytes;
        try {
            bytes = mapper.writeValueAsBytes(node);
        } catch (Exception e) {
            bytes = ("{\"error\":\"" + e.getMessage() + "\"}").getBytes(CharsetUtil.UTF_8);
        }
        FullHttpResponse res = new DefaultFullHttpResponse(HTTP_1_1, status, Unpooled.wrappedBuffer(bytes));
        res.headers().set(CONTENT_TYPE, "application/json; charset=utf-8");
        res.headers().set(CONTENT_LENGTH, res.content().readableBytes());
        ctx.writeAndFlush(res).addListener(ChannelFutureListener.CLOSE);
    }

    @Override
    public void exceptionCaught(ChannelHandlerContext ctx, Throwable cause) {
        log.debug("api err: {}", cause.getMessage());
        ctx.close();
    }
}
