package com.profile.service;

import com.profile.config.AppConfig;
import com.profile.model.UserProfileState;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import redis.clients.jedis.Jedis;
import redis.clients.jedis.JedisPool;
import redis.clients.jedis.JedisPoolConfig;

import java.util.HashMap;
import java.util.Map;

public class RedisProfileWriter {

    private static final Logger log = LoggerFactory.getLogger(RedisProfileWriter.class);

    private final JedisPool jedisPool;
    private final String keyPrefix;

    public RedisProfileWriter(AppConfig config) {
        JedisPoolConfig poolConfig = new JedisPoolConfig();
        poolConfig.setMaxTotal(32);
        poolConfig.setMaxIdle(8);
        poolConfig.setMinIdle(2);
        poolConfig.setTestOnBorrow(true);
        poolConfig.setTestOnReturn(true);

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

    public void writeProfile(UserProfileState state) {
        if (state == null || state.getUserId() == null) return;

        String key = keyPrefix + state.getUserId();
        Map<String, String> hash = new HashMap<>();

        for (Map.Entry<String, Double> entry : state.getTagScores().entrySet()) {
            hash.put(entry.getKey(), String.format("%.4f", entry.getValue()));
        }
        hash.put("_lastUpdate", String.valueOf(state.getLastUpdateTime()));
        hash.put("_eventCount", String.valueOf(state.getEventCount()));

        try (Jedis jedis = jedisPool.getResource()) {
            jedis.hset(key, hash);
            log.debug("Written profile for user: {}", state.getUserId());
        } catch (Exception e) {
            log.error("Failed to write profile for user: {}", state.getUserId(), e);
        }
    }

    public void close() {
        if (jedisPool != null && !jedisPool.isClosed()) {
            jedisPool.close();
        }
    }
}
