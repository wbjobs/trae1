package com.profile.store.storage;

import com.profile.store.bitmap.BitmapSerde;
import com.profile.store.config.ProfileProperties;
import org.roaringbitmap.RoaringBitmap;
import org.springframework.data.redis.core.RedisTemplate;
import org.springframework.stereotype.Component;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;

/**
 * 标签位图 Redis 存储层。
 * <p>
 * 负责：
 * <ul>
 *   <li>标签位图的序列化/反序列化存取</li>
 *   <li>标签基数的独立存储（用于查询优化器成本估算）</li>
 *   <li>本地 Caffeine 缓存（避免反复反序列化）</li>
 * </ul>
 * <p>
 * Redis Key 结构：
 * <pre>
 *   {keyPrefix}{tagName}         -> 位图字节数组 (String)
 *   {keyPrefix}{tagName}:card    -> 基数 (String)
 * </pre>
 */
@Component
public class TagBitmapRepository {

    private final RedisTemplate<String, byte[]> redisTemplate;
    private final ProfileProperties properties;
    private final Map<String, RoaringBitmap> localCache = new ConcurrentHashMap<>();
    private final Map<String, Long> cardinalityCache = new ConcurrentHashMap<>();

    public TagBitmapRepository(RedisTemplate<String, byte[]> redisTemplate, ProfileProperties properties) {
        this.redisTemplate = redisTemplate;
        this.properties = properties;
    }

    public Optional<RoaringBitmap> findByTag(String tagName) {
        RoaringBitmap cached = localCache.get(tagName);
        if (cached != null) {
            return Optional.of(cached);
        }
        byte[] data = redisTemplate.opsForValue().get(buildKey(tagName));
        if (data == null || data.length == 0) {
            return Optional.empty();
        }
        RoaringBitmap bitmap = BitmapSerde.deserialize(data);
        localCache.put(tagName, bitmap);
        cardinalityCache.put(tagName, (long) bitmap.getCardinality());
        return Optional.of(bitmap);
    }

    public RoaringBitmap getOrCreate(String tagName) {
        return findByTag(tagName).orElseGet(RoaringBitmap::new);
    }

    public void save(String tagName, RoaringBitmap bitmap) {
        byte[] data = BitmapSerde.serialize(bitmap);
        redisTemplate.opsForValue().set(buildKey(tagName), data);
        redisTemplate.opsForValue().set(buildCardinalityKey(tagName),
                String.valueOf(bitmap.getCardinality()));
        localCache.put(tagName, bitmap);
        cardinalityCache.put(tagName, (long) bitmap.getCardinality());
    }

    public void delete(String tagName) {
        redisTemplate.delete(buildKey(tagName));
        redisTemplate.delete(buildCardinalityKey(tagName));
        localCache.remove(tagName);
        cardinalityCache.remove(tagName);
    }

    public boolean exists(String tagName) {
        Boolean b = redisTemplate.hasKey(buildKey(tagName));
        return Boolean.TRUE.equals(b);
    }

    public long getCardinality(String tagName) {
        Long cached = cardinalityCache.get(tagName);
        if (cached != null) {
            return cached;
        }
        String cardStr = redisTemplate.opsForValue().get(buildCardinalityKey(tagName)) != null
                ? new String(Objects.requireNonNull(redisTemplate.opsForValue().get(buildCardinalityKey(tagName))))
                : null;
        if (cardStr != null) {
            long card = Long.parseLong(cardStr);
            cardinalityCache.put(tagName, card);
            return card;
        }
        return findByTag(tagName)
                .map(b -> {
                    long c = b.getCardinality();
                    cardinalityCache.put(tagName, c);
                    return c;
                })
                .orElse(0L);
    }

    public Map<String, Long> getCardinalities(Collection<String> tagNames) {
        Map<String, Long> result = new HashMap<>();
        List<String> missing = new ArrayList<>();
        for (String tag : tagNames) {
            Long cached = cardinalityCache.get(tag);
            if (cached != null) {
                result.put(tag, cached);
            } else {
                missing.add(tag);
            }
        }
        if (!missing.isEmpty()) {
            for (String tag : missing) {
                result.put(tag, getCardinality(tag));
            }
        }
        return result;
    }

    public void addUser(String tagName, int userId) {
        RoaringBitmap bitmap = getOrCreate(tagName);
        bitmap.add(userId);
        save(tagName, bitmap);
    }

    public void addUsers(String tagName, Collection<Integer> userIds) {
        RoaringBitmap bitmap = getOrCreate(tagName);
        for (Integer id : userIds) {
            bitmap.add(id);
        }
        save(tagName, bitmap);
    }

    public void removeUser(String tagName, int userId) {
        RoaringBitmap bitmap = getOrCreate(tagName);
        bitmap.remove(userId);
        save(tagName, bitmap);
    }

    public void removeUsers(String tagName, Collection<Integer> userIds) {
        RoaringBitmap bitmap = getOrCreate(tagName);
        for (Integer id : userIds) {
            bitmap.remove(id);
        }
        save(tagName, bitmap);
    }

    public Set<String> listTags() {
        String prefix = properties.getRedis().getKeyPrefix();
        Set<String> keys = redisTemplate.keys(prefix + "*");
        if (keys == null) return Collections.emptySet();
        Set<String> tags = new HashSet<>();
        for (String key : keys) {
            if (key.endsWith(":card")) continue;
            tags.add(key.substring(prefix.length()));
        }
        return tags;
    }

    public void evictCache(String tagName) {
        localCache.remove(tagName);
        cardinalityCache.remove(tagName);
    }

    public void evictAllCache() {
        localCache.clear();
        cardinalityCache.clear();
    }

    private String buildKey(String tagName) {
        return properties.getRedis().getKeyPrefix() + tagName;
    }

    private String buildCardinalityKey(String tagName) {
        return properties.getRedis().getKeyPrefix() + tagName + ":card";
    }
}
