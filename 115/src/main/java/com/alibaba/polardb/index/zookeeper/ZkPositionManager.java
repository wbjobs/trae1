package com.alibaba.polardb.index.zookeeper;

import com.alibaba.fastjson2.JSON;
import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ZookeeperConfig;
import com.alibaba.polardb.index.model.BinlogPosition;
import org.apache.curator.framework.CuratorFramework;
import org.apache.curator.framework.CuratorFrameworkFactory;
import org.apache.curator.framework.recipes.cache.PathChildrenCache;
import org.apache.curator.framework.recipes.cache.PathChildrenCacheEvent;
import org.apache.curator.framework.recipes.cache.PathChildrenCacheListener;
import org.apache.curator.retry.ExponentialBackoffRetry;
import org.apache.zookeeper.CreateMode;
import org.apache.zookeeper.data.Stat;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class ZkPositionManager {

    private static final Logger logger = LoggerFactory.getLogger(ZkPositionManager.class);

    private static final String POSITION_ROOT = "/positions";
    private static final String REBUILD_MARKER = "/rebuild_marker";

    @Autowired
    private GlobalIndexSyncProperties properties;

    private CuratorFramework client;
    private PathChildrenCache pathChildrenCache;
    private ConcurrentHashMap<String, BinlogPosition> positionCache = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        ZookeeperConfig config = properties.getZookeeper();
        try {
            client = CuratorFrameworkFactory.builder()
                    .connectString(config.getServers())
                    .namespace(config.getNamespace())
                    .connectionTimeoutMs(config.getConnectionTimeoutMs())
                    .sessionTimeoutMs(config.getSessionTimeoutMs())
                    .retryPolicy(new ExponentialBackoffRetry(
                            config.getRetryBaseSleepMs(),
                            config.getRetryMaxRetries()))
                    .build();
            client.start();

            ensurePathExists(POSITION_ROOT);
            ensurePathExists(REBUILD_MARKER);

            pathChildrenCache = new PathChildrenCache(client, POSITION_ROOT, true);
            pathChildrenCache.getListenable().addListener(new PathChildrenCacheListener() {
                @Override
                public void childEvent(CuratorFramework client, PathChildrenCacheEvent event) throws Exception {
                    handlePathChildrenEvent(event);
                }
            });
            pathChildrenCache.start();

            loadAllPositions();

            logger.info("ZooKeeper position manager initialized successfully");
        } catch (Exception e) {
            logger.error("Failed to initialize ZooKeeper position manager", e);
            throw new RuntimeException("Failed to initialize ZooKeeper position manager", e);
        }
    }

    private void ensurePathExists(String path) throws Exception {
        Stat stat = client.checkExists().forPath(path);
        if (stat == null) {
            client.create().creatingParentsIfNeeded().withMode(CreateMode.PERSISTENT).forPath(path);
        }
    }

    private void loadAllPositions() throws Exception {
        for (String child : client.getChildren().forPath(POSITION_ROOT)) {
            try {
                byte[] data = client.getData().forPath(POSITION_ROOT + "/" + child);
                if (data != null && data.length > 0) {
                    BinlogPosition position = JSON.parseObject(new String(data, StandardCharsets.UTF_8), BinlogPosition.class);
                    positionCache.put(child, position);
                    logger.info("Loaded binlog position for shard {}: {}", child, position);
                }
            } catch (Exception e) {
                logger.warn("Failed to load position for shard {}", child, e);
            }
        }
    }

    private void handlePathChildrenEvent(PathChildrenCacheEvent event) {
        try {
            String path = event.getData() != null ? event.getData().getPath() : null;
            if (path == null) {
                return;
            }
            String shardId = path.substring(POSITION_ROOT.length() + 1);

            switch (event.getType()) {
                case CHILD_ADDED:
                case CHILD_UPDATED:
                    byte[] data = event.getData().getData();
                    if (data != null && data.length > 0) {
                        BinlogPosition position = JSON.parseObject(new String(data, StandardCharsets.UTF_8), BinlogPosition.class);
                        positionCache.put(shardId, position);
                        logger.debug("Updated binlog position for shard {}: {}", shardId, position);
                    }
                    break;
                case CHILD_REMOVED:
                    positionCache.remove(shardId);
                    logger.info("Removed binlog position for shard {}", shardId);
                    break;
                default:
                    break;
            }
        } catch (Exception e) {
            logger.error("Error handling path children event", e);
        }
    }

    public void savePosition(String shardId, BinlogPosition position) {
        String path = POSITION_ROOT + "/" + shardId;
        try {
            String json = JSON.toJSONString(position);
            byte[] data = json.getBytes(StandardCharsets.UTF_8);

            Stat stat = client.checkExists().forPath(path);
            if (stat == null) {
                client.create().creatingParentsIfNeeded().withMode(CreateMode.PERSISTENT).forPath(path, data);
            } else {
                client.setData().forPath(path, data);
            }

            positionCache.put(shardId, position);
            logger.debug("Saved binlog position for shard {}: {}", shardId, position);
        } catch (Exception e) {
            logger.error("Failed to save binlog position for shard {}: {}", shardId, position, e);
            throw new RuntimeException("Failed to save binlog position", e);
        }
    }

    public BinlogPosition getPosition(String shardId) {
        BinlogPosition position = positionCache.get(shardId);
        if (position != null) {
            return position;
        }

        String path = POSITION_ROOT + "/" + shardId;
        try {
            Stat stat = client.checkExists().forPath(path);
            if (stat != null) {
                byte[] data = client.getData().forPath(path);
                if (data != null && data.length > 0) {
                    position = JSON.parseObject(new String(data, StandardCharsets.UTF_8), BinlogPosition.class);
                    positionCache.put(shardId, position);
                    return position;
                }
            }
        } catch (Exception e) {
            logger.warn("Failed to get position for shard {} from ZooKeeper", shardId, e);
        }

        return null;
    }

    public boolean isRebuildInProgress() {
        try {
            Stat stat = client.checkExists().forPath(REBUILD_MARKER);
            return stat != null;
        } catch (Exception e) {
            logger.error("Failed to check rebuild marker", e);
            return false;
        }
    }

    public void markRebuildStart() {
        try {
            String data = String.valueOf(System.currentTimeMillis());
            Stat stat = client.checkExists().forPath(REBUILD_MARKER);
            if (stat == null) {
                client.create().creatingParentsIfNeeded().withMode(CreateMode.EPHEMERAL)
                        .forPath(REBUILD_MARKER, data.getBytes(StandardCharsets.UTF_8));
            } else {
                client.setData().forPath(REBUILD_MARKER, data.getBytes(StandardCharsets.UTF_8));
            }
            logger.info("Marked rebuild start");
        } catch (Exception e) {
            logger.error("Failed to mark rebuild start", e);
            throw new RuntimeException("Failed to mark rebuild start", e);
        }
    }

    public void markRebuildComplete() {
        try {
            Stat stat = client.checkExists().forPath(REBUILD_MARKER);
            if (stat != null) {
                client.delete().forPath(REBUILD_MARKER);
            }
            logger.info("Marked rebuild complete");
        } catch (Exception e) {
            logger.error("Failed to mark rebuild complete", e);
        }
    }

    public void clearPosition(String shardId) {
        String path = POSITION_ROOT + "/" + shardId;
        try {
            Stat stat = client.checkExists().forPath(path);
            if (stat != null) {
                client.delete().forPath(path);
            }
            positionCache.remove(shardId);
            logger.info("Cleared binlog position for shard {}", shardId);
        } catch (Exception e) {
            logger.warn("Failed to clear position for shard {}", shardId, e);
        }
    }

    public void clearAllPositions() {
        try {
            for (String child : client.getChildren().forPath(POSITION_ROOT)) {
                try {
                    client.delete().forPath(POSITION_ROOT + "/" + child);
                } catch (Exception e) {
                    logger.warn("Failed to clear position for shard {}", child, e);
                }
            }
            positionCache.clear();
            logger.info("Cleared all binlog positions");
        } catch (Exception e) {
            logger.error("Failed to clear all positions", e);
        }
    }

    @PreDestroy
    public void destroy() {
        try {
            if (pathChildrenCache != null) {
                pathChildrenCache.close();
            }
        } catch (Exception e) {
            logger.warn("Error closing PathChildrenCache", e);
        }
        try {
            if (client != null) {
                client.close();
            }
        } catch (Exception e) {
            logger.warn("Error closing Curator client", e);
        }
        logger.info("ZooKeeper position manager destroyed");
    }
}
