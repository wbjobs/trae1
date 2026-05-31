package com.distributed.task.retry.job;

import com.distributed.task.common.lock.DistributedLockHelper;
import com.distributed.task.retry.service.RetryService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Slf4j
@Component
@RequiredArgsConstructor
public class RetryDispatchJob {

    private final RetryService retryService;
    private final StringRedisTemplate redisTemplate;

    @Value("${task.retry.lock-key:task:retry:dispatch:lock}")
    private String lockKey;

    @Value("${task.retry.lock-ttl-seconds:20}")
    private int lockTtlSeconds;

    @Scheduled(fixedDelayString = "${task.retry.scan-interval-ms:10000}")
    public void scanAndDispatch() {
        long leaseMs = lockTtlSeconds * 1000L;
        DistributedLockHelper lock = DistributedLockHelper.newLock(redisTemplate, lockKey, leaseMs);
        if (!lock.tryLock(500)) {
            return;
        }
        try {
            var tasks = retryService.scanDueTasks();
            if (tasks != null && !tasks.isEmpty()) {
                log.info("分发 {} 个待执行任务到优先级队列", tasks.size());
            }
        } catch (Exception e) {
            log.error("重试分发异常", e);
        } finally {
            lock.unlock();
        }
    }
}
