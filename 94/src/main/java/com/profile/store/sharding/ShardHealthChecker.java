package com.profile.store.sharding;

import io.lettuce.core.RedisClient;
import io.lettuce.core.RedisURI;
import io.lettuce.core.api.StatefulRedisConnection;
import io.lettuce.core.api.sync.RedisCommands;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.Duration;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 分片健康检查器：基于 Redis Sentinel 监控分片状态并执行故障转移。
 * <p>
 * 功能：
 * <ul>
 *   <li>定期（默认5秒）检查每个分片的连接状态</li>
 *   <li>如果分片不可用，尝试重连</li>
 *   <li>启用 Sentinel 时，通过 Sentinel 查询 master 地址并自动切换</li>
 *   <li>记录健康检查日志</li>
 * </ul>
 */
@Component
public class ShardHealthChecker {

    private static final Logger log = LoggerFactory.getLogger(ShardHealthChecker.class);

    private final ShardRedisConnectionFactory connectionFactory;
    private final ShardProperties properties;
    private final Map<Integer, RedisClient> sentinelClients = new ConcurrentHashMap<>();
    private final Map<Integer, StatefulRedisConnection<String, String>> sentinelConnections = new ConcurrentHashMap<>();
    private final Map<Integer, String> currentMasters = new ConcurrentHashMap<>();
    private volatile boolean running = false;

    public ShardHealthChecker(ShardRedisConnectionFactory connectionFactory, ShardProperties properties) {
        this.connectionFactory = connectionFactory;
        this.properties = properties;
    }

    @PostConstruct
    public void init() {
        running = true;
        if (properties.getSentinel().isEnabled() && !properties.getSentinel().getNodes().isEmpty()) {
            initSentinelConnections();
        }
    }

    @PreDestroy
    public void destroy() {
        running = false;
        for (Map.Entry<Integer, StatefulRedisConnection<String, String>> entry : sentinelConnections.entrySet()) {
            try {
                entry.getValue().close();
            } catch (Exception ignored) {
            }
        }
        sentinelConnections.clear();
        for (Map.Entry<Integer, RedisClient> entry : sentinelClients.entrySet()) {
            try {
                entry.getValue().shutdown();
            } catch (Exception ignored) {
            }
        }
        sentinelClients.clear();
        currentMasters.clear();
    }

    private void initSentinelConnections() {
        List<String> sentinelNodes = properties.getSentinel().getNodes();
        if (sentinelNodes.isEmpty()) return;

        for (ShardProperties.ShardNode node : properties.getNodes()) {
            for (String sentinelAddr : sentinelNodes) {
                try {
                    String[] parts = sentinelAddr.split(":");
                    String host = parts[0];
                    int port = parts.length > 1 ? Integer.parseInt(parts[1]) : 26379;

                    RedisURI.Builder uriBuilder = RedisURI.builder()
                            .withHost(host)
                            .withPort(port)
                            .withTimeout(Duration.ofSeconds(5));

                    if (properties.getSentinel().getPassword() != null
                            && !properties.getSentinel().getPassword().isEmpty()) {
                        uriBuilder.withPassword(properties.getSentinel().getPassword().toCharArray());
                    }

                    RedisClient client = RedisClient.create(uriBuilder.build());
                    StatefulRedisConnection<String, String> conn = client.connect();

                    sentinelClients.put(node.getShardId(), client);
                    sentinelConnections.put(node.getShardId(), conn);

                    String masterAddr = getMasterAddrFromSentinel(conn, properties.getSentinel().getMasterName());
                    if (masterAddr != null) {
                        currentMasters.put(node.getShardId(), masterAddr);
                    }
                    break;
                } catch (Exception e) {
                    log.warn("初始化分片 {} 的 Sentinel 连接失败: {}", node.getShardId(), e.getMessage());
                }
            }
        }
    }

    @Scheduled(fixedDelayString = "${profile.sharding.sentinel.check-interval-ms:5000}")
    public void checkAllShards() {
        if (!running) return;

        Map<Integer, Boolean> status = connectionFactory.getAllShardStatus();
        for (Map.Entry<Integer, Boolean> entry : status.entrySet()) {
            int shardId = entry.getKey();
            boolean active = entry.getValue();

            if (!active) {
                log.warn("分片 {} 不活跃，尝试重连", shardId);
                connectionFactory.tryReconnect(shardId);

                if (properties.getSentinel().isEnabled()) {
                    tryFailoverViaSentinel(shardId);
                }
            }
        }

        if (properties.getSentinel().isEnabled()) {
            checkMasterChanges();
        }
    }

    private void tryFailoverViaSentinel(int shardId) {
        StatefulRedisConnection<String, String> sentinelConn = sentinelConnections.get(shardId);
        if (sentinelConn == null) return;

        try {
            String masterName = properties.getSentinel().getMasterName() + "-" + shardId;
            String masterAddr = getMasterAddrFromSentinel(sentinelConn, masterName);
            if (masterAddr == null) {
                masterAddr = getMasterAddrFromSentinel(sentinelConn, properties.getSentinel().getMasterName());
            }

            if (masterAddr != null) {
                String oldMaster = currentMasters.get(shardId);
                if (!masterAddr.equals(oldMaster)) {
                    log.info("分片 {} master 变更: {} -> {}", shardId, oldMaster, masterAddr);
                    String[] parts = masterAddr.split(":");
                    String host = parts[0];
                    int port = parts.length > 1 ? Integer.parseInt(parts[1]) : 6379;

                    ShardProperties.ShardNode node = findNode(shardId);
                    if (node != null) {
                        node.setHost(host);
                        node.setPort(port);
                        connectionFactory.disconnect(shardId);
                        connectionFactory.connect(node);
                        currentMasters.put(shardId, masterAddr);
                    }
                }
            }
        } catch (Exception e) {
            log.warn("分片 {} Sentinel 故障转移失败: {}", shardId, e.getMessage());
        }
    }

    private void checkMasterChanges() {
        for (Map.Entry<Integer, StatefulRedisConnection<String, String>> entry : sentinelConnections.entrySet()) {
            int shardId = entry.getKey();
            StatefulRedisConnection<String, String> sentinelConn = entry.getValue();

            try {
                if (sentinelConn.isOpen()) {
                    String masterName = properties.getSentinel().getMasterName() + "-" + shardId;
                    String masterAddr = getMasterAddrFromSentinel(sentinelConn, masterName);
                    if (masterAddr == null) {
                        masterAddr = getMasterAddrFromSentinel(sentinelConn, properties.getSentinel().getMasterName());
                    }
                    if (masterAddr != null) {
                        currentMasters.put(shardId, masterAddr);
                    }
                }
            } catch (Exception ignored) {
            }
        }
    }

    private String getMasterAddrFromSentinel(StatefulRedisConnection<String, String> conn, String masterName) {
        try {
            RedisCommands<String, String> cmds = conn.sync();
            List<Map.Entry<String, String>> info = cmds.sentinelMaster(masterName);
            String ip = null;
            String port = null;
            String flags = null;
            for (Map.Entry<String, String> e : info) {
                if ("ip".equals(e.getKey())) ip = e.getValue();
                if ("port".equals(e.getKey())) port = e.getValue();
                if ("flags".equals(e.getKey())) flags = e.getValue();
            }
            if (ip != null && port != null && (flags == null || !flags.contains("down"))) {
                return ip + ":" + port;
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private ShardProperties.ShardNode findNode(int shardId) {
        for (ShardProperties.ShardNode node : properties.getNodes()) {
            if (node.getShardId() == shardId) {
                return node;
            }
        }
        return null;
    }

    public ShardHealthReport getHealthReport() {
        Map<Integer, Boolean> shardStatus = connectionFactory.getAllShardStatus();
        int activeCount = 0;
        int inactiveCount = 0;
        for (Boolean active : shardStatus.values()) {
            if (active) activeCount++;
            else inactiveCount++;
        }
        return new ShardHealthReport(
                shardStatus,
                activeCount,
                inactiveCount,
                properties.getSentinel().isEnabled(),
                currentMasters
        );
    }

    public static class ShardHealthReport {
        private final Map<Integer, Boolean> shardStatus;
        private final int activeCount;
        private final int inactiveCount;
        private final boolean sentinelEnabled;
        private final Map<Integer, String> currentMasters;

        public ShardHealthReport(Map<Integer, Boolean> shardStatus, int activeCount,
                                 int inactiveCount, boolean sentinelEnabled,
                                 Map<Integer, String> currentMasters) {
            this.shardStatus = shardStatus;
            this.activeCount = activeCount;
            this.inactiveCount = inactiveCount;
            this.sentinelEnabled = sentinelEnabled;
            this.currentMasters = currentMasters;
        }

        public Map<Integer, Boolean> getShardStatus() { return shardStatus; }
        public int getActiveCount() { return activeCount; }
        public int getInactiveCount() { return inactiveCount; }
        public boolean isSentinelEnabled() { return sentinelEnabled; }
        public Map<Integer, String> getCurrentMasters() { return currentMasters; }
        public boolean isHealthy() { return inactiveCount == 0; }
    }
}
