package com.profile.store.sharding;

import io.lettuce.core.RedisClient;
import io.lettuce.core.RedisURI;
import io.lettuce.core.api.StatefulRedisConnection;
import io.lettuce.core.api.sync.RedisCommands;
import io.lettuce.core.codec.ByteArrayCodec;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.springframework.stereotype.Component;

import java.time.Duration;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 分片 Redis 连接工厂：为每个分片维护独立的 Lettuce 连接。
 * <p>
 * 支持：
 * <ul>
 *   <li>启动时自动初始化所有分片连接</li>
 *   <li>动态添加分片（扩容时）</li>
 *   <li>连接健康检查与自动重连</li>
 *   <li>关闭时优雅释放所有连接</li>
 * </ul>
 */
@Component
public class ShardRedisConnectionFactory {

    private final ShardProperties properties;
    private final Map<Integer, RedisClient> clients = new ConcurrentHashMap<>();
    private final Map<Integer, StatefulRedisConnection<byte[], byte[]>> connections = new ConcurrentHashMap<>();
    private final Map<Integer, Boolean> activeFlags = new ConcurrentHashMap<>();

    public ShardRedisConnectionFactory(ShardProperties properties) {
        this.properties = properties;
    }

    @PostConstruct
    public synchronized void init() {
        List<ShardProperties.ShardNode> nodes = properties.getNodes();
        if (nodes.isEmpty()) {
            ShardProperties.ShardNode defaultNode = new ShardProperties.ShardNode();
            defaultNode.setShardId(0);
            defaultNode.setHost("localhost");
            defaultNode.setPort(6379);
            nodes.add(defaultNode);
            properties.setShardCount(1);
        }
        for (ShardProperties.ShardNode node : nodes) {
            connect(node);
        }
    }

    @PreDestroy
    public synchronized void destroy() {
        for (Map.Entry<Integer, StatefulRedisConnection<byte[], byte[]>> entry : connections.entrySet()) {
            try {
                entry.getValue().close();
            } catch (Exception ignored) {
            }
        }
        connections.clear();
        for (Map.Entry<Integer, RedisClient> entry : clients.entrySet()) {
            try {
                entry.getValue().shutdown();
            } catch (Exception ignored) {
            }
        }
        clients.clear();
        activeFlags.clear();
    }

    public synchronized void connect(ShardProperties.ShardNode node) {
        try {
            RedisURI.Builder uriBuilder = RedisURI.builder()
                    .withHost(node.getHost())
                    .withPort(node.getPort())
                    .withDatabase(node.getDatabase())
                    .withTimeout(Duration.ofSeconds(5));
            if (node.getPassword() != null && !node.getPassword().isEmpty()) {
                uriBuilder.withPassword(node.getPassword().toCharArray());
            }

            RedisClient client = RedisClient.create(uriBuilder.build());
            StatefulRedisConnection<byte[], byte[]> conn = client.connect(new ByteArrayCodec());

            clients.put(node.getShardId(), client);
            connections.put(node.getShardId(), conn);
            activeFlags.put(node.getShardId(), true);
        } catch (Exception e) {
            activeFlags.put(node.getShardId(), false);
            throw new RuntimeException("无法连接到分片 " + node.getShardId()
                    + " (" + node.getHost() + ":" + node.getPort() + "): " + e.getMessage(), e);
        }
    }

    public synchronized void disconnect(int shardId) {
        StatefulRedisConnection<byte[], byte[]> conn = connections.remove(shardId);
        if (conn != null) {
            try {
                conn.close();
            } catch (Exception ignored) {
            }
        }
        RedisClient client = clients.remove(shardId);
        if (client != null) {
            try {
                client.shutdown();
            } catch (Exception ignored) {
            }
        }
        activeFlags.remove(shardId);
    }

    public RedisCommands<byte[], byte[]> getConnection(int shardId) {
        StatefulRedisConnection<byte[], byte[]> conn = connections.get(shardId);
        if (conn == null || !conn.isOpen()) {
            tryReconnect(shardId);
            conn = connections.get(shardId);
        }
        if (conn == null) {
            throw new IllegalStateException("分片 " + shardId + " 连接不可用");
        }
        return conn.sync();
    }

    public boolean isShardActive(int shardId) {
        Boolean active = activeFlags.get(shardId);
        if (active == null || !active) {
            return false;
        }
        StatefulRedisConnection<byte[], byte[]> conn = connections.get(shardId);
        return conn != null && conn.isOpen();
    }

    public void setShardActive(int shardId, boolean active) {
        activeFlags.put(shardId, active);
    }

    public synchronized void tryReconnect(int shardId) {
        ShardProperties.ShardNode node = null;
        for (ShardProperties.ShardNode n : properties.getNodes()) {
            if (n.getShardId() == shardId) {
                node = n;
                break;
            }
        }
        if (node == null) {
            return;
        }
        try {
            disconnect(shardId);
            connect(node);
        } catch (Exception e) {
            activeFlags.put(shardId, false);
        }
    }

    public Map<Integer, Boolean> getAllShardStatus() {
        Map<Integer, Boolean> status = new ConcurrentHashMap<>();
        for (int shardId : connections.keySet()) {
            status.put(shardId, isShardActive(shardId));
        }
        return status;
    }
}
