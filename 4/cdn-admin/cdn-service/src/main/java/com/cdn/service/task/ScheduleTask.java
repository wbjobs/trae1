package com.cdn.service.task;

import com.cdn.service.cache.CacheService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

@Slf4j
@Component
@RequiredArgsConstructor
public class ScheduleTask {

    private final CacheService cacheService;

    @Scheduled(cron = "0 0 2 * * ?")
    public void cleanAbnormal() {
        log.info("定时任务：开始清理异常资源");
        cacheService.cleanAbnormalResources();
    }

    @Scheduled(cron = "0 */5 * * * ?")
    public void statsHeartbeat() {
        log.debug("统计心跳，当前缓存统计: {}", cacheService.getStats());
    }
}
