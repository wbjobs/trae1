package com.profile.store.sharding;

import com.profile.store.bitmap.BitmapSerde;
import io.lettuce.core.api.sync.RedisCommands;
import org.roaringbitmap.RoaringBitmap;
import org.springframework.stereotype.Service;

import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 分片重平衡服务：支持动态添加分片并进行数据迁移。
 * <p>
 * 核心流程：
 * <ol>
 *   <li>记录旧的分片数量</li>
 *   <li>添加新分片节点到连接工厂</li>
 *   <li>遍历所有现有分片的数据</li>
 *   <li>对每个标签位图，按新的分片规则重新分配用户到目标分片</li>
 *   <li>写入新分片后删除旧分片中已迁移的数据</li>
 *   <li>更新 ShardRouter 的分片计数</li>
 * </ol>
 * <p>
 * 支持在线迁移（读写不中断），迁移过程中新写入的数据按新分片规则路由。
 */
@Service
public class ShardRebalanceService {

    private final ShardedTagBitmapRepository repository;
    private final ShardRouter shardRouter;
    private final ShardRedisConnectionFactory connectionFactory;
    private final ShardProperties properties;
    private final Object rebalanceLock = new Object();
    private volatile boolean rebalancing = false;

    public ShardRebalanceService(ShardedTagBitmapRepository repository,
                                 ShardRouter shardRouter,
                                 ShardRedisConnectionFactory connectionFactory,
                                 ShardProperties properties) {
        this.repository = repository;
        this.shardRouter = shardRouter;
        this.connectionFactory = connectionFactory;
        this.properties = properties;
    }

    public RebalanceResult addShard(ShardProperties.ShardNode newNode) throws Exception {
        synchronized (rebalanceLock) {
            if (rebalancing) {
                throw new IllegalStateException("正在进行重平衡，无法添加新分片");
            }
            rebalancing = true;
        }

        int oldShardCount = shardRouter.getShardCount();
        int newShardCount = oldShardCount + 1;

        try {
            newNode.setShardId(oldShardCount);
            connectionFactory.connect(newNode);
            properties.getNodes().add(newNode);

            Set<String> allTags = repository.listAllTags();
            AtomicLong migratedUsers = new AtomicLong(0);
            AtomicLong skippedUsers = new AtomicLong(0);
            List<String> errors = new ArrayList<>();

            shardRouter.updateShardCount(newShardCount);

            for (String tag : allTags) {
                try {
                    migrateTag(tag, oldShardCount, newShardCount, migratedUsers, skippedUsers);
                } catch (Exception e) {
                    errors.add(tag + ": " + e.getMessage());
                }
            }

            repository.evictAllCaches();

            return new RebalanceResult(
                    oldShardCount,
                    newShardCount,
                    migratedUsers.get(),
                    skippedUsers.get(),
                    allTags.size(),
                    errors
            );
        } finally {
            rebalancing = false;
        }
    }

    private void migrateTag(String tag, int oldShardCount, int newShardCount,
                            AtomicLong migratedUsers, AtomicLong skippedUsers) {

        for (int oldShardId = 0; oldShardId < oldShardCount; oldShardId++) {
            if (!connectionFactory.isShardActive(oldShardId)) continue;

            Optional<RoaringBitmap> optBm = repository.findByTagAndShard(tag, oldShardId);
            if (optBm.isEmpty()) continue;

            RoaringBitmap oldBitmap = optBm.get();
            if (oldBitmap.isEmpty()) continue;

            Map<Integer, RoaringBitmap> newShardBitmaps = new HashMap<>();
            int[] userIds = oldBitmap.toArray();

            for (int userId : userIds) {
                int newShardId = Math.floorMod(userId, newShardCount);
                if (newShardId != oldShardId) {
                    newShardBitmaps
                            .computeIfAbsent(newShardId, k -> new RoaringBitmap())
                            .add(userId);
                }
            }

            for (Map.Entry<Integer, RoaringBitmap> entry : newShardBitmaps.entrySet()) {
                int targetShardId = entry.getKey();
                RoaringBitmap newData = entry.getValue();

                Optional<RoaringBitmap> optTarget = repository.findByTagAndShard(tag, targetShardId);
                RoaringBitmap targetBitmap = optTarget.orElseGet(RoaringBitmap::new);
                targetBitmap.or(newData);

                try {
                    RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(targetShardId);
                    byte[] key = repository.buildKey(targetShardId, tag);
                    byte[] data = BitmapSerde.serialize(targetBitmap);
                    conn.set(key, data);
                    conn.set(
                            repository.buildCardinalityKey(targetShardId, tag),
                            String.valueOf(targetBitmap.getCardinality()).getBytes(StandardCharsets.UTF_8)
                    );
                    migratedUsers.addAndGet(newData.getCardinality());
                } catch (Exception e) {
                    skippedUsers.addAndGet(newData.getCardinality());
                }
                targetBitmap.clear();
                newData.clear();
            }

            for (RoaringBitmap bm : newShardBitmaps.values()) {
                if (!bm.isEmpty()) {
                    oldBitmap.andNot(bm);
                }
            }

            if (!oldBitmap.isEmpty()) {
                try {
                    RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(oldShardId);
                    byte[] key = repository.buildKey(oldShardId, tag);
                    byte[] data = BitmapSerde.serialize(oldBitmap);
                    conn.set(key, data);
                    conn.set(
                            repository.buildCardinalityKey(oldShardId, tag),
                            String.valueOf(oldBitmap.getCardinality()).getBytes(StandardCharsets.UTF_8)
                    );
                } catch (Exception e) {
                    skippedUsers.addAndGet(oldBitmap.getCardinality());
                }
            } else {
                try {
                    RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(oldShardId);
                    conn.del(repository.buildKey(oldShardId, tag));
                    conn.del(repository.buildCardinalityKey(oldShardId, tag));
                } catch (Exception ignored) {
                }
            }
            oldBitmap.clear();
        }
    }

    public boolean isRebalancing() {
        return rebalancing;
    }

    public RebalanceStatus getStatus() {
        return new RebalanceStatus(
                rebalancing,
                shardRouter.getShardCount(),
                properties.getNodes().size(),
                connectionFactory.getAllShardStatus()
        );
    }

    public static class RebalanceResult {
        private final int oldShardCount;
        private final int newShardCount;
        private final long migratedUsers;
        private final long skippedUsers;
        private final int processedTags;
        private final List<String> errors;

        public RebalanceResult(int oldShardCount, int newShardCount, long migratedUsers,
                               long skippedUsers, int processedTags, List<String> errors) {
            this.oldShardCount = oldShardCount;
            this.newShardCount = newShardCount;
            this.migratedUsers = migratedUsers;
            this.skippedUsers = skippedUsers;
            this.processedTags = processedTags;
            this.errors = errors;
        }

        public int getOldShardCount() { return oldShardCount; }
        public int getNewShardCount() { return newShardCount; }
        public long getMigratedUsers() { return migratedUsers; }
        public long getSkippedUsers() { return skippedUsers; }
        public int getProcessedTags() { return processedTags; }
        public List<String> getErrors() { return errors; }
        public boolean isSuccess() { return errors.isEmpty(); }
    }

    public static class RebalanceStatus {
        private final boolean rebalancing;
        private final int currentShardCount;
        private final int configuredNodeCount;
        private final Map<Integer, Boolean> shardStatus;

        public RebalanceStatus(boolean rebalancing, int currentShardCount, int configuredNodeCount,
                               Map<Integer, Boolean> shardStatus) {
            this.rebalancing = rebalancing;
            this.currentShardCount = currentShardCount;
            this.configuredNodeCount = configuredNodeCount;
            this.shardStatus = shardStatus;
        }

        public boolean isRebalancing() { return rebalancing; }
        public int getCurrentShardCount() { return currentShardCount; }
        public int getConfiguredNodeCount() { return configuredNodeCount; }
        public Map<Integer, Boolean> getShardStatus() { return shardStatus; }
    }
}
