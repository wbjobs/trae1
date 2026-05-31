package com.sharding.sync.fix.service;

import com.alibaba.fastjson2.JSON;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.sharding.sync.check.entity.CheckDiff;
import com.sharding.sync.check.mapper.CheckDiffMapper;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.fix.entity.FixTask;
import com.sharding.sync.fix.mapper.FixTaskMapper;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.Executor;

@Slf4j
@Service
@RequiredArgsConstructor
public class FixService extends ServiceImpl<FixTaskMapper, FixTask> {

    private final CheckDiffMapper checkDiffMapper;
    private final ShardRuleService shardRuleService;
    private final JdbcTemplate jdbcTemplate;
    @Qualifier("syncTaskExecutor")
    private final Executor syncTaskExecutor;

    public FixTask submit(String logicTable, Long checkTaskId) {
        FixTask task = new FixTask();
        task.setTaskNo(UUID.randomUUID().toString().replace("-", ""));
        task.setLogicTable(logicTable);
        task.setCheckTaskId(checkTaskId);
        task.setStatus(SyncStatus.PENDING.getCode());
        task.setTotalCount(0L);
        task.setFixedCount(0L);
        task.setFailCount(0L);
        task.setCreateTime(LocalDateTime.now());
        task.setUpdateTime(LocalDateTime.now());
        save(task);
        syncTaskExecutor.execute(() -> runFix(task));
        return task;
    }

    public void runFix(FixTask task) {
        try {
            task.setStatus(SyncStatus.RUNNING.getCode());
            task.setStartTime(LocalDateTime.now());
            updateById(task);

            ShardRule rule = shardRuleService.getByLogicTable(task.getLogicTable());
            if (rule == null) {
                task.setStatus(SyncStatus.FAILED.getCode());
                task.setErrorMsg("分片规则不存在");
                task.setEndTime(LocalDateTime.now());
                updateById(task);
                return;
            }

            LambdaQueryWrapper<CheckDiff> wrapper = new LambdaQueryWrapper<>();
            wrapper.eq(CheckDiff::getLogicTable, task.getLogicTable());
            if (task.getCheckTaskId() != null) {
                wrapper.eq(CheckDiff::getTaskId, task.getCheckTaskId());
            }
            wrapper.and(w -> w.isNull(CheckDiff::getFixStatus).or().eq(CheckDiff::getFixStatus, 0));
            List<CheckDiff> diffs = checkDiffMapper.selectList(wrapper);

            task.setTotalCount((long) diffs.size());
            long fixed = 0;
            long fail = 0;
            for (CheckDiff diff : diffs) {
                try {
                    applyFix(rule, diff);
                    diff.setFixStatus(1);
                    checkDiffMapper.updateById(diff);
                    fixed++;
                } catch (Exception e) {
                    diff.setFixStatus(2);
                    checkDiffMapper.updateById(diff);
                    fail++;
                    log.warn("修复失败 diffId={} err={}", diff.getId(), e.getMessage());
                }
            }
            task.setFixedCount(fixed);
            task.setFailCount(fail);
            task.setStatus(fail == 0 ? SyncStatus.SUCCESS.getCode() : (fixed > 0 ? SyncStatus.PARTIAL.getCode() : SyncStatus.FAILED.getCode()));
            task.setEndTime(LocalDateTime.now());
            task.setUpdateTime(LocalDateTime.now());
            updateById(task);
        } catch (Exception e) {
            log.error("修复任务失败 taskNo={}", task.getTaskNo(), e);
            task.setStatus(SyncStatus.FAILED.getCode());
            task.setErrorMsg(e.getMessage());
            task.setEndTime(LocalDateTime.now());
            updateById(task);
        }
    }

    private void applyFix(ShardRule rule, CheckDiff diff) {
        String logicTable = rule.getLogicTable();
        String pk = rule.getPrimaryKey();
        if ("COUNT_MISMATCH".equals(diff.getDiffType())) {
            return;
        }
        Map<String, Object> source = parseMap(diff.getSourceData());
        Map<String, Object> target = parseMap(diff.getTargetData());
        if (source == null && target == null) {
            return;
        }
        if (target == null || target.isEmpty()) {
            if (source != null && source.containsKey(pk)) {
                jdbcTemplate.update("DELETE FROM " + logicTable + " WHERE " + pk + " = ?", source.get(pk));
            }
            return;
        }
        Map<String, Object> row = new HashMap<>(target);
        Object pkVal = diff.getPkValue() != null ? diff.getPkValue() : row.get(pk);
        if (pkVal == null) {
            return;
        }
        row.put(pk, pkVal);
        List<String> columns = new ArrayList<>(row.keySet());
        String cols = String.join(",", columns);
        String placeholders = columns.stream().map(c -> "?").reduce((a, b) -> a + "," + b).orElse("");
        List<String> updates = new ArrayList<>();
        for (String c : columns) {
            if (!c.equalsIgnoreCase(pk)) {
                updates.add(c + " = VALUES(" + c + ")");
            }
        }
        String sql = "INSERT INTO " + logicTable + " (" + cols + ") VALUES (" + placeholders + ")";
        if (!updates.isEmpty()) {
            sql += " ON DUPLICATE KEY UPDATE " + String.join(",", updates);
        }
        Object[] args = columns.stream().map(row::get).toArray();
        jdbcTemplate.update(sql, args);
    }

    private Map<String, Object> parseMap(String json) {
        if (!StringUtils.hasText(json)) {
            return null;
        }
        try {
            return JSON.parseObject(json, Map.class);
        } catch (Exception e) {
            return null;
        }
    }

    public FixTask getByTaskNo(String taskNo) {
        return getOne(new LambdaQueryWrapper<FixTask>().eq(FixTask::getTaskNo, taskNo));
    }

    public IPage<FixTask> page(Page<FixTask> page, String logicTable, String status) {
        LambdaQueryWrapper<FixTask> wrapper = new LambdaQueryWrapper<>();
        if (StringUtils.hasText(logicTable)) {
            wrapper.eq(FixTask::getLogicTable, logicTable);
        }
        if (StringUtils.hasText(status)) {
            wrapper.eq(FixTask::getStatus, status);
        }
        wrapper.orderByDesc(FixTask::getCreateTime);
        return page(page, wrapper);
    }

    public Map<String, Object> getStatus(String taskNo) {
        FixTask task = getByTaskNo(taskNo);
        if (task == null) {
            return null;
        }
        Map<String, Object> m = new HashMap<>();
        m.put("taskNo", task.getTaskNo());
        m.put("logicTable", task.getLogicTable());
        m.put("status", task.getStatus());
        m.put("totalCount", task.getTotalCount());
        m.put("fixedCount", task.getFixedCount());
        m.put("failCount", task.getFailCount());
        m.put("startTime", task.getStartTime());
        m.put("endTime", task.getEndTime());
        m.put("errorMsg", task.getErrorMsg());
        return m;
    }
}
