package com.cdn.service.cache;

import com.cdn.common.exception.BusinessException;
import com.cdn.domain.entity.CacheConfig;
import com.cdn.domain.mapper.CacheConfigMapper;
import com.cdn.domain.mapper.CdnResourceMapper;
import com.cdn.domain.entity.CdnResource;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.redis.core.RedisTemplate;
import org.springframework.data.redis.core.script.DefaultRedisScript;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;

@Slf4j
@Service
@RequiredArgsConstructor
public class CacheService {

    private static final String RESOURCE_CACHE_PREFIX = "cdn:resource:";
    private static final String HIT_KEY = "cdn:stat:hit";
    private static final String MISS_KEY = "cdn:stat:miss";
    private static final String LEVEL_HOT = "hot";
    private static final String LEVEL_NORMAL = "normal";
    private static final String LEVEL_COLD = "cold";

    private final RedisTemplate<String, Object> redisTemplate;
    private final CacheConfigMapper configMapper;
    private final CdnResourceMapper resourceMapper;

    public void putResource(String url, Object data) {
        CacheConfig config = matchConfig(url);
        int ttl = config != null ? config.getTtlSeconds() : 3600;
        String level = resolveLevel(url);
        String key = RESOURCE_CACHE_PREFIX + url;
        redisTemplate.opsForValue().set(key, data, ttl, TimeUnit.SECONDS);
        redisTemplate.opsForHash().put("cdn:resource:level", url, level);
    }

    public Object getResource(String url) {
        String key = RESOURCE_CACHE_PREFIX + url;
        Object val = redisTemplate.opsForValue().get(key);
        if (val != null) {
            redisTemplate.opsForValue().increment(HIT_KEY);
            recordAccess(url);
        } else {
            redisTemplate.opsForValue().increment(MISS_KEY);
        }
        return val;
    }

    @Async("refreshExecutor")
    public void asyncRefreshBatch(List<String> urls, String operator,
                                   java.util.function.Consumer<Map<String, Object>> callback) {
        log.info("开始批量刷新资源，数量={}, 操作人={}", urls.size(), operator);
        Map<String, Object> result = new HashMap<>();
        int success = 0;
        int fail = 0;
        List<String> failUrls = new ArrayList<>();
        long start = System.currentTimeMillis();

        for (String url : urls) {
            try {
                String key = RESOURCE_CACHE_PREFIX + url;
                redisTemplate.delete(key);
                redisTemplate.opsForHash().delete("cdn:resource:level", url);
                success++;
                log.debug("刷新成功: {}", url);
            } catch (Exception e) {
                fail++;
                failUrls.add(url);
                log.error("刷新失败: {}", url, e);
            }
        }
        long cost = System.currentTimeMillis() - start;
        result.put("total", urls.size());
        result.put("success", success);
        result.put("fail", fail);
        result.put("failUrls", failUrls);
        result.put("costTime", cost);
        result.put("operator", operator);
        callback.accept(result);
    }

    public Map<String, Object> getStats() {
        Map<String, Object> stats = new HashMap<>();
        Long hit = getLong(HIT_KEY);
        Long miss = getLong(MISS_KEY);
        long total = hit + miss;
        double rate = total == 0 ? 0.0 : (hit * 100.0 / total);
        stats.put("hitCount", hit);
        stats.put("missCount", miss);
        stats.put("totalCount", total);
        stats.put("hitRate", Math.round(rate * 100.0) / 100.0);

        Set<String> keys = Optional.ofNullable(redisTemplate.keys(RESOURCE_CACHE_PREFIX + "*"))
                .orElse(Collections.emptySet());
        stats.put("cachedCount", keys.size());

        Map<Object, Object> levelMap = redisTemplate.opsForHash().entries("cdn:resource:level");
        long hot = levelMap.values().stream().filter(v -> LEVEL_HOT.equals(v)).count();
        long normal = levelMap.values().stream().filter(v -> LEVEL_NORMAL.equals(v)).count();
        long cold = levelMap.values().stream().filter(v -> LEVEL_COLD.equals(v)).count();
        Map<String, Long> levelStats = new HashMap<>();
        levelStats.put("hot", hot);
        levelStats.put("normal", normal);
        levelStats.put("cold", cold);
        stats.put("levelStats", levelStats);

        return stats;
    }

    private Long getLong(String key) {
        Object v = redisTemplate.opsForValue().get(key);
        return v == null ? 0L : Long.parseLong(v.toString());
    }

    private CacheConfig matchConfig(String url) {
        List<CacheConfig> configs = configMapper.selectList(
                new LambdaQueryWrapper<CacheConfig>().eq(CacheConfig::getStatus, 1));
        for (CacheConfig c : configs) {
            if (Pattern.matches(c.getResourcePattern(), url)) {
                return c;
            }
        }
        return null;
    }

    private String resolveLevel(String url) {
        Object count = redisTemplate.opsForHash().get("cdn:resource:access", url);
        long c = count == null ? 0 : Long.parseLong(count.toString());
        if (c >= 100) return LEVEL_HOT;
        if (c >= 10) return LEVEL_NORMAL;
        return LEVEL_COLD;
    }

    private void recordAccess(String url) {
        redisTemplate.opsForHash().increment("cdn:resource:access", url, 1);
    }

    public void cleanAbnormalResources() {
        LambdaQueryWrapper<CdnResource> qw = new LambdaQueryWrapper<>();
        qw.eq(CdnResource::getStatus, 0);
        List<CdnResource> abnormal = resourceMapper.selectList(qw);
        int cleaned = 0;
        for (CdnResource r : abnormal) {
            try {
                redisTemplate.delete(RESOURCE_CACHE_PREFIX + r.getResourceUrl());
                redisTemplate.opsForHash().delete("cdn:resource:level", r.getResourceUrl());
                redisTemplate.opsForHash().delete("cdn:resource:access", r.getResourceUrl());
                r.setStatus(2);
                r.setUpdateTime(LocalDateTime.now());
                resourceMapper.updateById(r);
                cleaned++;
            } catch (Exception e) {
                log.error("清理异常资源失败: {}", r.getResourceUrl(), e);
            }
        }
        log.info("异常资源清理完成，共清理{}条", cleaned);
    }
}
