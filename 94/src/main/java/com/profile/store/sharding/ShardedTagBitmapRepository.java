package com.profile.store.sharding;

import com.profile.store.bitmap.BitmapSerde;
import io.lettuce.core.api.sync.RedisCommands;
import org.roaringbitmap.RoaringBitmap;
import org.springframework.stereotype.Component;

import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 分片标签位图存储仓库：按分片路由存储/查询位图数据。
 * <p>
 * 每个分片独立存储该分片范围内用户的位图数据，查询时需要聚合所有分片结果。
 * Redis Key 结构：{prefix}{shardId}:{tagName}
 */
@Component
public class ShardedTagBitmapRepository {

    private static final String KEY_PREFIX = "profile:tag:";
    private static final String CARD_SUFFIX = ":card";

    private final ShardRedisConnectionFactory connectionFactory;
    private final ShardRouter shardRouter;
    private final Map<Integer, Map<String, RoaringBitmap>> localCaches = new ConcurrentHashMap<>();
    private final Map<Integer, Map<String, Long>> cardinalityCaches = new ConcurrentHashMap<>();

    public ShardedTagBitmapRepository(ShardRedisConnectionFactory connectionFactory, ShardRouter shardRouter) {
        this.connectionFactory = connectionFactory;
        this.shardRouter = shardRouter;
    }

    public void save(String tagName, int userId) {
        int shardId = shardRouter.route(userId);
        RoaringBitmap bitmap = findByTagAndShard(tagName, shardId).orElseGet(RoaringBitmap::new);
        bitmap.add(userId);
        saveToShard(tagName, shardId, bitmap);
    }

    public void saveUsers(String tagName, Collection<Integer> userIds) {
        Map<Integer, List<Integer>> shardGroups = new HashMap<>();
        for (Integer userId : userIds) {
            int shardId = shardRouter.route(userId);
            shardGroups.computeIfAbsent(shardId, k -> new ArrayList<>()).add(userId);
        }
        for (Map.Entry<Integer, List<Integer>> entry : shardGroups.entrySet()) {
            int shardId = entry.getKey();
            RoaringBitmap bitmap = findByTagAndShard(tagName, shardId).orElseGet(RoaringBitmap::new);
            for (Integer id : entry.getValue()) {
                bitmap.add(id);
            }
            saveToShard(tagName, shardId, bitmap);
        }
    }

    public void remove(String tagName, int userId) {
        int shardId = shardRouter.route(userId);
        RoaringBitmap bitmap = findByTagAndShard(tagName, shardId).orElseGet(RoaringBitmap::new);
        bitmap.remove(userId);
        saveToShard(tagName, shardId, bitmap);
    }

    public void removeUsers(String tagName, Collection<Integer> userIds) {
        Map<Integer, List<Integer>> shardGroups = new HashMap<>();
        for (Integer userId : userIds) {
            int shardId = shardRouter.route(userId);
            shardGroups.computeIfAbsent(shardId, k -> new ArrayList<>()).add(userId);
        }
        for (Map.Entry<Integer, List<Integer>> entry : shardGroups.entrySet()) {
            int shardId = entry.getKey();
            RoaringBitmap bitmap = findByTagAndShard(tagName, shardId).orElseGet(RoaringBitmap::new);
            for (Integer id : entry.getValue()) {
                bitmap.remove(id);
            }
            saveToShard(tagName, shardId, bitmap);
        }
    }

    public Optional<RoaringBitmap> findByTagAndShard(String tagName, int shardId) {
        Map<String, RoaringBitmap> cache = localCaches.computeIfAbsent(shardId, k -> new ConcurrentHashMap<>());
        RoaringBitmap cached = cache.get(tagName);
        if (cached != null) {
            return Optional.of(cached);
        }

        if (!connectionFactory.isShardActive(shardId)) {
            return Optional.empty();
        }

        try {
            RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(shardId);
            byte[] key = buildKey(shardId, tagName);
            byte[] data = conn.get(key);
            if (data == null || data.length == 0) {
                return Optional.empty();
            }
            RoaringBitmap bitmap = BitmapSerde.deserialize(data);
            cache.put(tagName, bitmap);
            getCardinalityCache(shardId).put(tagName, (long) bitmap.getCardinality());
            return Optional.of(bitmap);
        } catch (Exception e) {
            connectionFactory.setShardActive(shardId, false);
            return Optional.empty();
        }
    }

    public RoaringBitmap getOrCreate(String tagName, int shardId) {
        return findByTagAndShard(tagName, shardId).orElseGet(RoaringBitmap::new);
    }

    public Map<Integer, RoaringBitmap> findAllShards(String tagName) {
        Map<Integer, RoaringBitmap> result = new HashMap<>();
        for (int shardId : shardRouter.allShardIds()) {
            if (connectionFactory.isShardActive(shardId)) {
                findByTagAndShard(tagName, shardId).ifPresent(bm -> result.put(shardId, bm));
            }
        }
        return result;
    }

    public Map<Integer, Long> getCardinality(String tagName) {
        Map<Integer, Long> result = new HashMap<>();
        for (int shardId : shardRouter.allShardIds()) {
            if (connectionFactory.isShardActive(shardId)) {
                long card = getCardinalityFromShard(tagName, shardId);
                result.put(shardId, card);
            }
        }
        return result;
    }

    public long getTotalCardinality(String tagName) {
        long total = 0;
        for (int shardId : shardRouter.allShardIds()) {
            if (connectionFactory.isShardActive(shardId)) {
                total += getCardinalityFromShard(tagName, shardId);
            }
        }
        return total;
    }

    public void deleteTag(String tagName) {
        for (int shardId : shardRouter.allShardIds()) {
            if (!connectionFactory.isShardActive(shardId)) continue;
            try {
                RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(shardId);
                conn.del(buildKey(shardId, tagName));
                conn.del(buildCardinalityKey(shardId, tagName));
            } catch (Exception e) {
                connectionFactory.setShardActive(shardId, false);
            }
        }
        for (Map<String, RoaringBitmap> cache : localCaches.values()) {
            cache.remove(tagName);
        }
        for (Map<String, Long> cache : cardinalityCaches.values()) {
            cache.remove(tagName);
        }
    }

    public Set<String> listTags(int shardId) {
        if (!connectionFactory.isShardActive(shardId)) {
            return Collections.emptySet();
        }
        try {
            RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(shardId);
            String pattern = KEY_PREFIX + shardId + ":*";
            var keys = conn.keys(pattern.getBytes(StandardCharsets.UTF_8));
            Set<String> tags = new HashSet<>();
            for (byte[] keyBytes : keys) {
                String key = new String(keyBytes, StandardCharsets.UTF_8);
                if (key.endsWith(CARD_SUFFIX)) continue;
                String tagPart = key.substring((KEY_PREFIX + shardId + ":").length());
                tags.add(tagPart);
            }
            return tags;
        } catch (Exception e) {
            connectionFactory.setShardActive(shardId, false);
            return Collections.emptySet();
        }
    }

    public Set<String> listAllTags() {
        Set<String> allTags = new HashSet<>();
        for (int shardId : shardRouter.allShardIds()) {
            allTags.addAll(listTags(shardId));
        }
        return allTags;
    }

    public void evictCache(int shardId, String tagName) {
        Map<String, RoaringBitmap> cache = localCaches.get(shardId);
        if (cache != null) cache.remove(tagName);
        Map<String, Long> cardCache = cardinalityCaches.get(shardId);
        if (cardCache != null) cardCache.remove(tagName);
    }

    public void evictAllCaches() {
        localCaches.clear();
        cardinalityCaches.clear();
    }

    private void saveToShard(String tagName, int shardId, RoaringBitmap bitmap) {
        if (!connectionFactory.isShardActive(shardId)) {
            connectionFactory.tryReconnect(shardId);
        }
        try {
            RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(shardId);
            byte[] data = BitmapSerde.serialize(bitmap);
            conn.set(buildKey(shardId, tagName), data);
            conn.set(buildCardinalityKey(shardId, tagName),
                    String.valueOf(bitmap.getCardinality()).getBytes(StandardCharsets.UTF_8));
            localCaches.computeIfAbsent(shardId, k -> new ConcurrentHashMap<>()).put(tagName, bitmap);
            getCardinalityCache(shardId).put(tagName, (long) bitmap.getCardinality());
        } catch (Exception e) {
            connectionFactory.setShardActive(shardId, false);
        }
    }

    private long getCardinalityFromShard(String tagName, int shardId) {
        Map<String, Long> cache = getCardinalityCache(shardId);
        Long cached = cache.get(tagName);
        if (cached != null) {
            return cached;
        }
        if (!connectionFactory.isShardActive(shardId)) {
            return 0;
        }
        try {
            RedisCommands<byte[], byte[]> conn = connectionFactory.getConnection(shardId);
            byte[] data = conn.get(buildCardinalityKey(shardId, tagName));
            if (data != null) {
                long card = Long.parseLong(new String(data, StandardCharsets.UTF_8));
                cache.put(tagName, card);
                return card;
            }
            return findByTagAndShard(tagName, shardId)
                    .map(b -> {
                        long c = b.getCardinality();
                        cache.put(tagName, c);
                        return c;
                    })
                    .orElse(0L);
        } catch (Exception e) {
            connectionFactory.setShardActive(shardId, false);
            return 0;
        }
    }

    private Map<String, Long> getCardinalityCache(int shardId) {
        return cardinalityCaches.computeIfAbsent(shardId, k -> new ConcurrentHashMap<>());
    }

    public byte[] buildKey(int shardId, String tagName) {
        return (KEY_PREFIX + shardId + ":" + tagName).getBytes(StandardCharsets.UTF_8);
    }

    public byte[] buildCardinalityKey(int shardId, String tagName) {
        return (KEY_PREFIX + shardId + ":" + tagName + CARD_SUFFIX).getBytes(StandardCharsets.UTF_8);
    }

    public ShardRouter getShardRouter() {
        return shardRouter;
    }

    public ShardRedisConnectionFactory getConnectionFactory() {
        return connectionFactory;
    }
}
