package com.quant.gateway;

import com.quant.gateway.aggregate.AggregatePusher;
import com.quant.gateway.aggregate.AggregateRule;
import com.quant.gateway.aggregate.SlidingWindowAggregator;
import com.quant.gateway.api.RestApiServer;
import com.quant.gateway.cache.MarketCache;
import com.quant.gateway.config.GatewayConfig;
import com.quant.gateway.session.ClientSession;
import com.quant.gateway.session.NettyClientSession;
import com.quant.gateway.session.SessionRegistry;
import com.quant.gateway.session.SubscriptionManager;
import com.quant.gateway.transport.DefaultFeedDispatcher;
import com.quant.gateway.transport.UdpFeedReceiver;
import com.quant.gateway.ws.WebSocketServer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/**
 * Level-2 行情分发网关启动入口。
 *
 * 组件关系:
 *   UdpFeedReceiver (行情源)
 *         |  (每笔 TickData)
 *         +----------> MarketCache (缓存 + 全局流水号)
 *         |                     |
 *         v                     v
 *   SlidingWindowAggregator  FeedDispatcher (订阅过滤 + 扇出)
 *         |                     |
 *         v                     v
 *   AggregatePusher        WebSocketServer -> ClientSession
 *         |
 *         +-----------------> 订阅聚合数据的客户端
 *
 *   RestApiServer  (独立端口, 查询运行状态 + 聚合规则管理)
 */
public final class GatewayServer {

    private static final Logger log = LoggerFactory.getLogger(GatewayServer.class);

    public static void main(String[] args) throws Exception {
        log.info("============================================");
        log.info(" Level-2 Market Data Gateway starting ...");
        log.info("  WS   : ws://{}:{}{}", GatewayConfig.WS_HOST, GatewayConfig.WS_PORT, GatewayConfig.WS_PATH);
        log.info("  REST : http://{}:{}/api/clients", GatewayConfig.WS_HOST, GatewayConfig.REST_PORT);
        log.info("  UDP  : {}:{}", GatewayConfig.UDP_GROUP, GatewayConfig.UDP_PORT);
        log.info("============================================");

        ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(2);
        MarketCache cache = new MarketCache();
        SessionRegistry sessions = new SessionRegistry(scheduler);
        SubscriptionManager subscriptions = new SubscriptionManager();
        DefaultFeedDispatcher dispatcher = new DefaultFeedDispatcher(subscriptions, cache);

        // ============ 聚合引擎 ============
        SlidingWindowAggregator aggregator = new SlidingWindowAggregator();
        AggregatePusher aggregatePusher = new AggregatePusher(aggregator);
        aggregatePusher.setSessionFinder(sessions::findOnline);

        // 注册默认聚合规则
        registerDefaultRules(aggregator);

        // UDP 行情接收器 (同时送入聚合引擎)
        UdpFeedReceiver receiver = new UdpFeedReceiver(cache, dispatcher, aggregator);
        receiver.start();

        // 内置模拟器 (每秒 10 万笔 tick)
        UdpFeedReceiver.startSimulator();

        // WebSocket 服务 (支持聚合订阅)
        WebSocketServer ws = new WebSocketServer(
                GatewayConfig.WS_HOST, GatewayConfig.WS_PORT,
                sessions, subscriptions, cache, dispatcher, aggregatePusher);
        ws.start();

        // REST API 服务 (含聚合规则管理)
        RestApiServer rest = new RestApiServer(
                GatewayConfig.REST_PORT, sessions, subscriptions, cache, dispatcher,
                aggregator, aggregatePusher);
        rest.start();

        // 行情源健康检查 + 降级切换
        scheduler.scheduleAtFixedRate(() -> {
            if (!cache.isFeedAlive()) {
                dispatcher.onFeedDown();
            } else if (cache.isDegraded()) {
                dispatcher.onFeedUp();
            }
        }, 1, 2, TimeUnit.SECONDS);

        // 周期打印运行状态
        scheduler.scheduleAtFixedRate(() -> {
            log.info("stats: online={} offlineCached={} subs={} degraded={} backPressured={} pausedUpstream={} rules={} aggregates={}",
                    sessions.onlineCount(), sessions.offlineCount(),
                    subscriptions.totalSubscribers(), cache.isDegraded(),
                    countBackPressured(sessions), dispatcher.pausedCount(),
                    aggregator.ruleCount(), aggregator.getTotalAggregates());
        }, 10, 30, TimeUnit.SECONDS);

        // JVM 关闭钩子
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            log.info("shutting down gateway ...");
            scheduler.shutdownNow();
        }));

        Thread.currentThread().join();
    }

    /**
     * 注册默认聚合规则 (示例):
     *   rule_1s: 1秒滑动窗口, 所有股票, 加权均价 + 成交量 + 大单统计
     *   rule_tech: 1秒窗口, tech板块, 仅 price > 100 的 tick
     *   rule_big: 5秒窗口, 大单统计 (成交量 > 10000)
     */
    private static void registerDefaultRules(SlidingWindowAggregator aggregator) {
        try {
            // 规则1: 全市场 1 秒聚合
            aggregator.registerRule(AggregateRule.builder()
                    .id("rule_1s")
                    .name("全市场1秒聚合")
                    .dsl("sum(volume), wavg(price), count() where volume > 0")
                    .windowSizeMs(1000)
                    .pushIntervalMs(1000)
                    .bigOrderThreshold(10000)
                    .deltaEnabled(true)
                    .build());

            // 规则2: tech 板块 1 秒聚合 (价格 > 100)
            aggregator.registerRule(AggregateRule.builder()
                    .id("rule_tech")
                    .name("科技板块1秒聚合")
                    .dsl("sum(volume) totalVolume, wavg(price) avgPrice where sector = 'tech' and price > 100")
                    .windowSizeMs(1000)
                    .pushIntervalMs(1000)
                    .sectors(java.util.Collections.singletonList("tech"))
                    .deltaEnabled(true)
                    .build());

            // 规则3: 5 秒大单统计
            aggregator.registerRule(AggregateRule.builder()
                    .id("rule_big")
                    .name("5秒大单统计")
                    .dsl("sum(volume) bigVolume, count() bigCount where volume > 10000")
                    .windowSizeMs(5000)
                    .pushIntervalMs(1000)
                    .bigOrderThreshold(10000)
                    .deltaEnabled(true)
                    .build());

            log.info("registered 3 default aggregate rules");
        } catch (Exception e) {
            log.error("failed to register default rules: {}", e.getMessage(), e);
        }
    }

    private static int countBackPressured(SessionRegistry sessions) {
        int cnt = 0;
        for (ClientSession s : sessions.allOnline().values()) {
            if (s instanceof NettyClientSession && ((NettyClientSession) s).isBackPressured()) {
                cnt++;
            }
        }
        return cnt;
    }
}
