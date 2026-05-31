package com.alibaba.polardb.index.join.cache;

import com.alibaba.polardb.index.join.model.JoinQuery;
import com.alibaba.polardb.index.join.model.JoinResult;
import com.github.benmanes.caffeine.cache.Cache;
import com.github.benmanes.caffeine.cache.Caffeine;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

@Component
public class QueryCache {
    private static final Logger logger = LoggerFactory.getLogger(QueryCache.class);

    @Value("${global-index.sync.join.cache.enabled:true}")
    private boolean cacheEnabled;

    @Value("${global-index.sync.join.cache.ttl-seconds:5}")
    private int cacheTtlSeconds;

    @Value("${global-index.sync.join.cache.max-size:1000}")
    private int cacheMaxSize;

    private Cache<String, CachedResult> resultCache;
    private final AtomicLong hitCount = new AtomicLong(0);
    private final AtomicLong missCount = new AtomicLong(0);

    public static class CachedResult {
        private final JoinResult result;
        private final long createdAt;

        public CachedResult(JoinResult result) {
            this.result = result;
            this.createdAt = System.currentTimeMillis();
        }

        public JoinResult getResult() {
            return result;
        }

        public long getCreatedAt() {
            return createdAt;
        }

        public long getAgeMs() {
            return System.currentTimeMillis() - createdAt;
        }
    }

    @PostConstruct
    public void init() {
        if (cacheEnabled) {
            resultCache = Caffeine.newBuilder()
                    .maximumSize(cacheMaxSize)
                    .expireAfterWrite(cacheTtlSeconds, TimeUnit.SECONDS)
                    .recordStats()
                    .build();
            logger.info("Query cache initialized: ttl={}s, max-size={}",
                    cacheTtlSeconds, cacheMaxSize);
        } else {
            logger.info("Query cache disabled");
        }
    }

    public JoinResult get(JoinQuery query) {
        if (!cacheEnabled || resultCache == null) {
            return null;
        }

        String cacheKey = query.toCacheKey();
        CachedResult cached = resultCache.getIfPresent(cacheKey);

        if (cached != null) {
            hitCount.incrementAndGet();
            JoinResult result = cached.getResult();
            result.setFromCache(true);
            logger.debug("Cache hit for query: {}, age: {}ms",
                    cacheKey, cached.getAgeMs());
            return result;
        }

        missCount.incrementAndGet();
        return null;
    }

    public void put(JoinQuery query, JoinResult result) {
        if (!cacheEnabled || resultCache == null || !result.isSuccess()) {
            return;
        }

        String cacheKey = query.toCacheKey();
        resultCache.put(cacheKey, new CachedResult(result));
        logger.debug("Cached result for query: {}", cacheKey);
    }

    public void invalidate(JoinQuery query) {
        if (resultCache == null) return;
        String cacheKey = query.toCacheKey();
        resultCache.invalidate(cacheKey);
        logger.debug("Invalidated cache for query: {}", cacheKey);
    }

    public void invalidateAll() {
        if (resultCache == null) return;
        resultCache.invalidateAll();
        logger.info("All cache entries invalidated");
    }

    public long getHitCount() {
        return hitCount.get();
    }

    public long getMissCount() {
        return missCount.get();
    }

    public double getHitRate() {
        long total = hitCount.get() + missCount.get();
        if (total == 0) return 0.0;
        return hitCount.get() * 1.0 / total;
    }

    public long getSize() {
        if (resultCache == null) return 0;
        return resultCache.estimatedSize();
    }

    public com.github.benmanes.caffeine.cache.stats.CacheStats getStats() {
        if (resultCache == null) return null;
        return resultCache.stats();
    }

    public boolean isCacheEnabled() {
        return cacheEnabled;
    }
}
