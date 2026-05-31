package com.sharding.sync.check.service;

import com.sharding.sync.check.entity.CheckTask;
import com.sharding.sync.check.mapper.CheckTaskMapper;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.fix.service.FixService;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
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
public class CheckScheduler {

    private final ShardRuleService shardRuleService;
    private final CheckService checkService;
    private final CheckTaskMapper checkTaskMapper;
    private final FixService fixService;

    @Scheduled(cron = "${sync.schedule.check-cron:0 0 4 * * ?}")
    public void scheduledCheck() {
        List<ShardRule> rules = shardRuleService.listAll();
        log.info("定时数据校验启动, 共 {} 个分片表", rules.size());
        for (ShardRule rule : rules) {
            if (rule.getStatus() == null || rule.getStatus() != 1) {
                continue;
            }
            try {
                CheckTask task = new CheckTask();
                task.setTaskNo(UUID.randomUUID().toString().replace("-", ""));
                task.setLogicTable(rule.getLogicTable());
                task.setCheckType("ALL");
                task.setStatus(SyncStatus.PENDING.getCode());
                task.setTotalCount(0L);
                task.setDiffCount(0L);
                task.setCreateTime(LocalDateTime.now());
                task.setUpdateTime(LocalDateTime.now());
                checkTaskMapper.insert(task);
                checkService.runCheck(task);
                if (task.getDiffCount() != null && task.getDiffCount() > 0) {
                    log.info("校验发现差异 table={} diffCount={}, 触发自动修复",
                            rule.getLogicTable(), task.getDiffCount());
                    try {
                        fixService.submit(rule.getLogicTable(), task.getId());
                    } catch (Exception fixEx) {
                        log.warn("自动修复提交失败 table={}: {}", rule.getLogicTable(), fixEx.getMessage());
                    }
                }
            } catch (Exception e) {
                log.warn("定时校验失败 table={}: {}", rule.getLogicTable(), e.getMessage());
            }
        }
    }

    @Scheduled(cron = "${sync.schedule.check-auto-fix-cron:0 15 4 * * ?}")
    public void scheduledAutoFix() {
        List<CheckTask> tasks = checkTaskMapper.selectList(
                new com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper<CheckTask>()
                        .eq(CheckTask::getStatus, SyncStatus.PARTIAL.getCode())
                        .gt(CheckTask::getDiffCount, 0)
                        .ge(CheckTask::getCreateTime, LocalDateTime.now().minusHours(6))
                        .orderByAsc(CheckTask::getCreateTime)
                        .last("limit 10"));
        if (tasks.isEmpty()) {
            return;
        }
        log.info("定时自动修复启动, 发现 {} 个待修复校验任务", tasks.size());
        for (CheckTask task : tasks) {
            try {
                fixService.submit(task.getLogicTable(), task.getId());
            } catch (Exception e) {
                log.warn("自动修复触发失败 taskNo={}: {}", task.getTaskNo(), e.getMessage());
            }
        }
    }
}
