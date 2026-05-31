package com.profile.store.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

import java.time.Duration;

@Configuration
@ConfigurationProperties(prefix = "profile")
public class ProfileProperties {

    private Redis redis = new Redis();
    private Cache cache = new Cache();

    public Redis getRedis() { return redis; }
    public Cache getCache() { return cache; }

    public static class Redis {
        private String keyPrefix = "profile:tag:";

        public String getKeyPrefix() { return keyPrefix; }
        public void setKeyPrefix(String keyPrefix) { this.keyPrefix = keyPrefix; }
    }

    public static class Cache {
        private long maximumSize = 256;
        private Duration expireAfterWrite = Duration.ofMinutes(5);

        public long getMaximumSize() { return maximumSize; }
        public void setMaximumSize(long maximumSize) { this.maximumSize = maximumSize; }
        public Duration getExpireAfterWrite() { return expireAfterWrite; }
        public void setExpireAfterWrite(Duration expireAfterWrite) { this.expireAfterWrite = expireAfterWrite; }
    }
}
