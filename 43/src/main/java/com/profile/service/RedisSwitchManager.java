package com.profile.service;

import com.profile.config.AppConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;

public class RedisSwitchManager {

    private static final Logger log = LoggerFactory.getLogger(RedisSwitchManager.class);

    private final RedisProfileReader primaryReader;
    private final RedisProfileReader newReader;
    private final AppConfig config;
    private final AtomicReference<RedisProfileReader> activeReader;

    public RedisSwitchManager(AppConfig config) {
        this.config = config;
        this.primaryReader = new RedisProfileReader(config);
        this.newReader = createNewReader(config);
        this.activeReader = new AtomicReference<>(primaryReader);
    }

    private RedisProfileReader createNewReader(AppConfig config) {
        AppConfig newConfig = new AppConfig();
        newConfig.setRedisHost(
                config.getNewRedisHost() != null && !config.getNewRedisHost().isEmpty()
                        ? config.getNewRedisHost()
                        : config.getRedisHost());
        newConfig.setRedisPort(config.getNewRedisPort() > 0
                ? config.getNewRedisPort()
                : config.getRedisPort());
        newConfig.setRedisPassword(config.getNewRedisPassword() != null
                ? config.getNewRedisPassword()
                : config.getRedisPassword());
        newConfig.setRedisDatabase(config.getNewRedisDatabase() > 0
                ? config.getNewRedisDatabase()
                : config.getRedisDatabase());
        newConfig.setRedisKeyPrefix(config.getNewRedisKeyPrefix() != null
                ? config.getNewRedisKeyPrefix()
                : "user:profile:new:");
        return new RedisProfileReader(newConfig);
    }

    public List<Map<String, Object>> getProfile(String userId) {
        return activeReader.get().getProfile(userId);
    }

    public boolean switchToNew() {
        RedisProfileReader current = activeReader.get();
        if (current == newReader) {
            log.warn("Already reading from new Redis, no switch needed.");
            return false;
        }
        if (activeReader.compareAndSet(primaryReader, newReader)) {
            config.setUseNewRedis(true);
            log.info("Switched to NEW Redis instance successfully.");
            return true;
        }
        return false;
    }

    public boolean switchBack() {
        RedisProfileReader current = activeReader.get();
        if (current == primaryReader) {
            log.warn("Already reading from primary Redis, no switch needed.");
            return false;
        }
        if (activeReader.compareAndSet(newReader, primaryReader)) {
            config.setUseNewRedis(false);
            log.info("Switched back to PRIMARY Redis instance successfully.");
            return true;
        }
        return false;
    }

    public Map<String, Object> compareProfiles(String userId) {
        List<Map<String, Object>> primaryTags = primaryReader.getProfile(userId);
        List<Map<String, Object>> newTags = newReader.getProfile(userId);

        Map<String, Double> primaryMap = new HashMap<>();
        for (Map<String, Object> t : primaryTags) {
            primaryMap.put((String) t.get("tag"), (Double) t.get("score"));
        }

        Map<String, Double> newMap = new HashMap<>();
        for (Map<String, Object> t : newTags) {
            newMap.put((String) t.get("tag"), (Double) t.get("score"));
        }

        List<String> diffs = new ArrayList<>();
        for (String tag : primaryMap.keySet()) {
            double p = primaryMap.get(tag);
            double n = newMap.getOrDefault(tag, 0.0);
            double delta = Math.abs(p - n);
            if (delta > 0.01) {
                diffs.add(tag + ": primary=" + p + ", new=" + n + ", delta=" + delta);
            }
        }

        Map<String, Object> result = new HashMap<>();
        result.put("userId", userId);
        result.put("primaryTagCount", primaryTags.size());
        result.put("newTagCount", newTags.size());
        result.put("differences", diffs);
        result.put("diffCount", diffs.size());
        return result;
    }

    public String getActiveSource() {
        return activeReader.get() == newReader ? "new" : "primary";
    }

    public void close() {
        primaryReader.close();
        newReader.close();
    }
}
