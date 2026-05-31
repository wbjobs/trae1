package com.sharding.sync.fix.service;

import com.sharding.sync.check.entity.CheckDiff;
import com.sharding.sync.check.mapper.CheckDiffMapper;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.DataSyncService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.List;

@Slf4j
@Component
@RequiredArgsConstructor
public class FixScheduler {

    private final FixService fixService;
    private final ShardRuleService shardRuleService;
    private final CheckDiffMapper checkDiffMapper;

    @Scheduled(cron = "${sync.schedule.fix-cron:0 30 4 * * ?}")
    public void scheduledFix() {
        List<ShardRule> rules = shardRuleService.listAll();
        log.info("定时修复启动, 共 {} 个分片表", rules.size());
        for (ShardRule rule : rules) {
            if (rule.getStatus() == null || rule.getStatus() != 1) {
                continue;
            }
            try {
                List<CheckDiff> pending = checkDiffMapper.selectList(
                        new com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper<CheckDiff>()
                                .eq(CheckDiff::getLogicTable, rule.getLogicTable())
                                .and(w -> w.isNull(CheckDiff::getFixStatus).or().eq(CheckDiff::getFixStatus, 0))
                                .last("limit 10"));
                if (!pending.isEmpty()) {
                    fixService.submit(rule.getLogicTable(), null);
                }
            } catch (Exception e) {
                log.warn("定时修复失败 table={}: {}", rule.getLogicTable(), e.getMessage());
            }
        }
    }
}
