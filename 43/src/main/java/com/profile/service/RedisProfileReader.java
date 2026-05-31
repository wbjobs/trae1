package com.profile.service;

import com.profile.config.AppConfig;
import com.profile.model.Tag;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisPool;
import redis.clients.jedis.JedisPoolConfig;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public class RedisProfileReader {

    private static final Logger log = LoggerFactory.getLogger(RedisProfileReader.class);

    private final JedisPool jedisPool;
    private final String keyPrefix;

    public RedisProfileReader(AppConfig config) {
        JedisPoolConfig poolConfig = new JedisPoolConfig();
        poolConfig.setMaxTotal(64);
        poolConfig.setMaxIdle(16);
        poolConfig.setMinIdle(4);
        poolConfig.setTestOnBorrow(true);
        poolConfig.setTestOnReturn(true);
        poolConfig.setTestWhileIdle(true);
        poolConfig.setTimeBetweenEvictionRunsMillis(60000);

        this.keyPrefix = config.getRedisKeyPrefix();

        if (config.getRedisPassword() != null && !config.getRedisPassword().isEmpty()) {
            this.jedisPool = new JedisPool(poolConfig,
                    config.getRedisHost(),
                    config.getRedisPort(),
                    2000,
                    config.getRedisPassword(),
                    config.getRedisDatabase());
        } else {
            this.jedisPool = new JedisPool(poolConfig,
                    config.getRedisHost(),
                    config.getRedisPort(),
                    2000,
                    null,
                    config.getRedisDatabase());
        }
    }

    public List<Map<String, Object>> getProfile(String userId) {
        String key = keyPrefix + userId;
        Map<String, String> raw;
        try (Jedis jedis = jedisPool.getResource()) {
            raw = jedis.hgetAll(key);
        } catch (Exception e) {
            log.error("Failed to read profile for user: {}", userId, e);
            return new ArrayList<>();
        }

        if (raw == null || raw.isEmpty()) {
            return new ArrayList<>();
        }

        List<Map<String, Object>> result = new ArrayList<>();
        Map<String, Double> scores = new HashMap<>();

        for (Map.Entry<String, String> entry : raw.entrySet()) {
            String field = entry.getKey();
            if (field.startsWith("_")) continue;
            try {
                scores.put(field, Double.parseDouble(entry.getValue()));
            } catch (NumberFormatException ignored) {
            }
        }

        List<Map.Entry<String, Double>> sorted = new ArrayList<>(scores.entrySet());
        sorted.sort((a, b) -> Double.compare(b.getValue(), a.getValue()));

        for (Map.Entry<String, Double> entry : sorted) {
            Map<String, Object> item = new LinkedHashMap<>();
            item.put("tag", entry.getKey());
            item.put("score", Math.round(entry.getValue() * 100.0) / 100.0);
            result.add(item);
        }

        return result;
    }

    public void close() {
        if (jedisPool != null && !jedisPool.isClosed()) {
            jedisPool.close();
        }
    }
}
