package com.sharding.sync.sync.service;

import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import com.sharding.sync.sync.entity.SyncTask;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

@Slf4j
@Component
@RequiredArgsConstructor
public class SyncScheduler {

    private final ShardRuleService shardRuleService;
    private final DataSyncService dataSyncService;
    @Qualifier("syncTaskExecutor")
    private final Executor syncTaskExecutor;

    @Scheduled(cron = "${sync.schedule.full-cron:0 0 2 * * ?}")
    public void scheduledFullSync() {
        List<ShardRule> rules = shardRuleService.listAll();
        log.info("定时全量同步启动, 共 {} 个分片表", rules.size());
        for (ShardRule rule : rules) {
            if (rule.getStatus() == null || rule.getStatus() != 1) {
                continue;
            }
            try {
                SyncTask task = dataSyncService.submitFullSyncForSchedule(rule.getLogicTable(), null, null, new HashMap<>());
                syncTaskExecutor.execute(() -> dataSyncService.runSync(task));
            } catch (Exception e) {
                log.warn("定时全量同步提交失败 table={}: {}", rule.getLogicTable(), e.getMessage());
            }
        }
    }

    @Scheduled(cron = "${sync.schedule.incremental-cron:0 */5 * * * ?}")
    public void scheduledIncrementalSync() {
        List<ShardRule> rules = shardRuleService.listAll();
        for (ShardRule rule : rules) {
            if (rule.getStatus() == null || rule.getStatus() != 1) {
                continue;
            }
            try {
                Map<String, Object> params = new HashMap<>();
                params.put("since", LocalDateTime.now().minusMinutes(10));
                SyncTask task = dataSyncService.submitIncrementalSyncForSchedule(rule.getLogicTable(), null, null, params);
                syncTaskExecutor.execute(() -> dataSyncService.runSync(task));
            } catch (Exception e) {
                log.warn("定时增量同步提交失败 table={}: {}", rule.getLogicTable(), e.getMessage());
            }
        }
    }
}
