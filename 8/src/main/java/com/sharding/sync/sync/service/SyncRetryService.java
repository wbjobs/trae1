package com.sharding.sync.sync.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.config.MyCatProperties;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.impl.SyncTaskServiceImpl;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.List;
import java.util.UUID;

@Slf4j
@Component
@RequiredArgsConstructor
public class SyncRetryService {

    private final SyncTaskServiceImpl syncTaskService;
    private final ShardRuleService shardRuleService;
    private final DataSyncService dataSyncService;
    private final MyCatProperties myCatProperties;

    private static final int MAX_RETRY_TIMES = 3;

    @Scheduled(cron = "${sync.schedule.retry-cron:0 */10 * * * ?}")
    public void scheduledRetry() {
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        wrapper.and(w -> w.eq(SyncTask::getStatus, SyncStatus.FAILED.getCode())
                        .or()
                        .eq(SyncTask::getStatus, SyncStatus.PARTIAL.getCode()))
                .and(w -> w.isNull(SyncTask::getRetryOf).or().eq(SyncTask::getRetryOf, 0))
                .ge(SyncTask::getCreateTime, LocalDateTime.now().minusHours(24))
                .orderByAsc(SyncTask::getCreateTime)
                .last("limit 20");
        List<SyncTask> failedTasks = syncTaskService.list(wrapper);
        if (failedTasks.isEmpty()) {
            return;
        }
        log.info("定时重试发现 {} 个失败任务", failedTasks.size());
        for (SyncTask task : failedTasks) {
            try {
                retryTask(task);
            } catch (Exception e) {
                log.warn("重试任务失败 taskNo={}: {}", task.getTaskNo(), e.getMessage());
            }
        }
    }

    public SyncTask retryTask(SyncTask original) {
        int retryCount = countRetries(original.getTaskNo());
        if (retryCount >= myCatProperties.getSync().getRetryTimes()) {
            log.warn("任务已达到最大重试次数 {}, 不再重试 taskNo={}", MAX_RETRY_TIMES, original.getTaskNo());
            return null;
        }
        ShardRule rule = shardRuleService.getByLogicTable(original.getLogicTable());
        if (rule == null || (rule.getStatus() != null && rule.getStatus() != 1)) {
            log.warn("分片规则不可用, 跳过重试 table={}", original.getLogicTable());
            return null;
        }
        SyncTask retryTask = new SyncTask();
        retryTask.setTaskNo(UUID.randomUUID().toString().replace("-", ""));
        retryTask.setTaskName(original.getTaskName() + "_retry" + (retryCount + 1));
        retryTask.setSyncType(original.getSyncType());
        retryTask.setLogicTable(original.getLogicTable());
        retryTask.setSourceDs(original.getSourceDs());
        retryTask.setTargetDs(original.getTargetDs());
        retryTask.setTriggerMode("RETRY");
        retryTask.setStatus(SyncStatus.PENDING.getCode());
        retryTask.setTotalCount(0L);
        retryTask.setSuccessCount(0L);
        retryTask.setFailCount(0L);
        retryTask.setParams(original.getParams());
        retryTask.setRetryOf(original.getId());
        retryTask.setCreateTime(LocalDateTime.now());
        retryTask.setUpdateTime(LocalDateTime.now());
        syncTaskService.save(retryTask);
        log.info("创建重试任务 retryTaskNo={} originalTaskNo={} retryCount={}",
                retryTask.getTaskNo(), original.getTaskNo(), retryCount + 1);
        new Thread(() -> {
            try {
                Thread.sleep(myCatProperties.getSync().getRetryIntervalMs());
            } catch (InterruptedException ignore) {
            }
            dataSyncService.runSync(retryTask);
        }).start();
        return retryTask;
    }

    private int countRetries(String originalTaskNo) {
        SyncTask original = syncTaskService.getByTaskNo(originalTaskNo);
        if (original == null) {
            return 0;
        }
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        wrapper.eq(SyncTask::getRetryOf, original.getId());
        return (int) syncTaskService.count(wrapper);
    }
}
