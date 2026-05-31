package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class JoinConfig {
    private boolean enabled = true;
    private Long timeoutMs = 30000L;
    private int executorThreads = 8;
    private int executorMaxThreads = 32;
    private boolean predicatePushdownEnabled = true;
    private boolean resultCacheEnabled = true;
    private int cacheTtlSeconds = 5;
    private int cacheMaxSize = 1000;
    private boolean statsEnabled = true;
    private long statsRefreshIntervalMs = 60000L;
    private long hashJoinThreshold = 1000L;
    private String defaultJoinAlgorithm = "AUTO";

    public static class CacheConfig {
        private boolean enabled = true;
        private int ttlSeconds = 5;
        private int maxSize = 1000;

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        public int getTtlSeconds() {
            return ttlSeconds;
        }

        public void setTtlSeconds(int ttlSeconds) {
            this.ttlSeconds = ttlSeconds;
        }

        public int getMaxSize() {
            return maxSize;
        }

        public void setMaxSize(int maxSize) {
            this.maxSize = maxSize;
        }
    }
}
