package com.distributed.task.common.lock;

import lombok.extern.slf4j.Slf4j;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.data.redis.core.script.DefaultRedisScript;

import java.util.Collections;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

@Slf4j
public class DistributedLockHelper {

    private final StringRedisTemplate redisTemplate;
    private final String lockKey;
    private final long leaseTimeMs;
    private final String token;
    private final AtomicInteger reentrantCount;

    private static final ConcurrentHashMap<String, LockHolder> HOLDER_MAP = new ConcurrentHashMap<>();
    private static final ScheduledExecutorService WATCHDOG =
            Executors.newScheduledThreadPool(2, r -> {
                Thread t = new Thread(r, "distributed-lock-watchdog");
                t.setDaemon(true);
                return t;
            });

    private static final DefaultRedisScript<Long> UNLOCK_SCRIPT;

    static {
        UNLOCK_SCRIPT = new DefaultRedisScript<>();
        UNLOCK_SCRIPT.setScriptText(
                "if redis.call('get', KEYS[1]) == ARGV[1] then " +
                "  return redis.call('del', KEYS[1]) " +
                "else return 0 end");
        UNLOCK_SCRIPT.setResultType(Long.class);
    }

    private static final DefaultRedisScript<Long> RENEW_SCRIPT;

    static {
        RENEW_SCRIPT = new DefaultRedisScript<>();
        RENEW_SCRIPT.setScriptText(
                "if redis.call('get', KEYS[1]) == ARGV[1] then " +
                "  return redis.call('expire', KEYS[1], ARGV[2]) " +
                "else return 0 end");
        RENEW_SCRIPT.setResultType(Long.class);
    }

    private DistributedLockHelper(StringRedisTemplate redisTemplate, String lockKey, long leaseTimeMs) {
        this.redisTemplate = redisTemplate;
        this.lockKey = lockKey;
        this.leaseTimeMs = leaseTimeMs;
        this.token = UUID.randomUUID().toString().replace("-", "");
        this.reentrantCount = new AtomicInteger(0);
    }

    public static DistributedLockHelper newLock(StringRedisTemplate redisTemplate, String lockKey, long leaseTimeMs) {
        return new DistributedLockHelper(redisTemplate, lockKey, leaseTimeMs);
    }

    public boolean tryLock(long waitTimeMs) {
        long deadline = System.currentTimeMillis() + waitTimeMs;
        long sleepMs = Math.min(50, leaseTimeMs / 3);
        while (System.currentTimeMillis() < deadline) {
            if (tryLock()) {
                return true;
            }
            try {
                Thread.sleep(sleepMs);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return tryLock();
    }

    public boolean tryLock() {
        LockHolder existing = HOLDER_MAP.get(lockKey);
        if (existing != null && existing.token.equals(this.token)) {
            existing.reentrantCount.incrementAndGet();
            return true;
        }
        Boolean ok = redisTemplate.opsForValue()
                .setIfAbsent(lockKey, token, leaseTimeMs, TimeUnit.MILLISECONDS);
        if (Boolean.TRUE.equals(ok)) {
            LockHolder holder = new LockHolder(token, reentrantCount);
            reentrantCount.set(1);
            HOLDER_MAP.put(lockKey, holder);
            startWatchdog();
            return true;
        }
        return false;
    }

    public void unlock() {
        LockHolder holder = HOLDER_MAP.get(lockKey);
        if (holder == null || !holder.token.equals(this.token)) {
            return;
        }
        int count = holder.reentrantCount.decrementAndGet();
        if (count > 0) {
            return;
        }
        try {
            redisTemplate.execute(UNLOCK_SCRIPT,
                    Collections.singletonList(lockKey), token);
        } catch (Exception e) {
            log.warn("释放分布式锁失败 key={}", lockKey, e);
        }
        HOLDER_MAP.remove(lockKey);
    }

    private void startWatchdog() {
        long renewIntervalMs = leaseTimeMs / 3;
        WATCHDOG.scheduleAtFixedRate(() -> {
            LockHolder h = HOLDER_MAP.get(lockKey);
            if (h == null || !h.token.equals(token)) {
                return;
            }
            try {
                Long res = redisTemplate.execute(RENEW_SCRIPT,
                        Collections.singletonList(lockKey),
                        token, String.valueOf(leaseTimeMs / 1000));
                if (Long.valueOf(0).equals(res)) {
                    log.debug("锁已失效，停止续约 key={}", lockKey);
                    HOLDER_MAP.remove(lockKey);
                }
            } catch (Exception e) {
                log.warn("锁续约异常 key={}", lockKey, e);
            }
        }, renewIntervalMs, renewIntervalMs, TimeUnit.MILLISECONDS);
    }

    private static class LockHolder {
        final String token;
        final AtomicInteger reentrantCount;

        LockHolder(String token, AtomicInteger reentrantCount) {
            this.token = token;
            this.reentrantCount = reentrantCount;
        }
    }
}
