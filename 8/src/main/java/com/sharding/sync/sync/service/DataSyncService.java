package com.sharding.sync.sync.service;

import com.sharding.sync.common.BusinessException;
import com.sharding.sync.common.ResultCode;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.common.SyncType;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import com.sharding.sync.sync.dto.SyncTaskDTO;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.impl.SyncTaskServiceImpl;
import com.alibaba.fastjson2.JSON;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executor;
import java.util.concurrent.atomic.AtomicLong;

@Slf4j
@Service
@RequiredArgsConstructor
public class DataSyncService {

    private final JdbcTemplate jdbcTemplate;
    private final ShardRuleService shardRuleService;
    private final SyncTaskServiceImpl syncTaskService;
    @Qualifier("syncTaskExecutor")
    private final Executor syncTaskExecutor;

    private final Map<String, Boolean> runningFlags = new ConcurrentHashMap<>();

    private final ThreadLocal<List<Long>> lastBinlogFailedEventIds = ThreadLocal.withInitial(ArrayList::new);

    public SyncTask submitFullSync(String logicTable, String sourceDs, String targetDs, Map<String, Object> params) {
        return submitSync(logicTable, SyncType.FULL.getCode(), sourceDs, targetDs, params, "MANUAL", true);
    }

    public SyncTask submitIncrementalSync(String logicTable, String sourceDs, String targetDs, Map<String, Object> params) {
        return submitSync(logicTable, SyncType.INCREMENTAL.getCode(), sourceDs, targetDs, params, "MANUAL", true);
    }

    public SyncTask submitBinlogSync(String logicTable, String sourceDs, String targetDs, Map<String, Object> params) {
        return submitSync(logicTable, SyncType.BINLOG.getCode(), sourceDs, targetDs, params, "MANUAL", true);
    }

    public SyncTask submitFullSyncForSchedule(String logicTable, String sourceDs, String targetDs, Map<String, Object> params) {
        return submitSync(logicTable, SyncType.FULL.getCode(), sourceDs, targetDs, params, "SCHEDULED", false);
    }

    public SyncTask submitIncrementalSyncForSchedule(String logicTable, String sourceDs, String targetDs, Map<String, Object> params) {
        return submitSync(logicTable, SyncType.INCREMENTAL.getCode(), sourceDs, targetDs, params, "SCHEDULED", false);
    }

    public List<Long> getLastBinlogFailedEventIds() {
        List<Long> ids = new ArrayList<>(lastBinlogFailedEventIds.get());
        lastBinlogFailedEventIds.remove();
        return ids;
    }

    public SyncTask runBinlogSyncForWorker(String logicTable, Map<String, Object> params) {
        String syncType = SyncType.BINLOG.getCode();
        String key = logicTable + ":" + syncType;
        if (Boolean.TRUE.equals(runningFlags.get(key))) {
            throw new BusinessException(ResultCode.SYNC_RUNNING);
        }
        SyncTaskDTO dto = new SyncTaskDTO();
        dto.setLogicTable(logicTable);
        dto.setSyncType(syncType);
        dto.setTriggerMode("WORKER");
        dto.setParams(params);
        SyncTask task = syncTaskService.submit(dto);
        runningFlags.put(key, true);
        runSync(task);
        return task;
    }

    private SyncTask submitSync(String logicTable, String syncType, String sourceDs, String targetDs,
                                Map<String, Object> params, String triggerMode, boolean autoExecute) {
        String key = logicTable + ":" + syncType;
        if (Boolean.TRUE.equals(runningFlags.get(key))) {
            throw new BusinessException(ResultCode.SYNC_RUNNING);
        }
        SyncTaskDTO dto = new SyncTaskDTO();
        dto.setLogicTable(logicTable);
        dto.setSyncType(syncType);
        dto.setSourceDs(sourceDs);
        dto.setTargetDs(targetDs);
        dto.setTriggerMode(triggerMode);
        dto.setParams(params);
        SyncTask task = syncTaskService.submit(dto);
        runningFlags.put(key, true);
        if (autoExecute) {
            syncTaskExecutor.execute(() -> runSync(task));
        }
        return task;
    }

    public void runSync(SyncTask task) {
        String key = task.getLogicTable() + ":" + task.getSyncType();
        try {
            task.setStartTime(LocalDateTime.now());
            task.setStatus(SyncStatus.RUNNING.getCode());
            syncTaskService.updateById(task);

            ShardRule rule = shardRuleService.getByLogicTable(task.getLogicTable());
            if (rule == null) {
                throw new BusinessException(ResultCode.SHARD_RULE_NOT_FOUND);
            }

            AtomicLong success = new AtomicLong(0);
            AtomicLong fail = new AtomicLong(0);
            lastBinlogFailedEventIds.set(new ArrayList<>());

            if (SyncType.FULL.getCode().equals(task.getSyncType())) {
                doFullSync(task, rule, success, fail);
            } else if (SyncType.INCREMENTAL.getCode().equals(task.getSyncType())) {
                doIncrementalSync(task, rule, success, fail);
            } else if (SyncType.BINLOG.getCode().equals(task.getSyncType())) {
                doBinlogSync(task, rule, success, fail);
            }

            String finalStatus = fail.get() == 0 ? SyncStatus.SUCCESS.getCode()
                    : (success.get() > 0 ? SyncStatus.PARTIAL.getCode() : SyncStatus.FAILED.getCode());
            syncTaskService.finishTask(task.getId(), finalStatus, null);
        } catch (Exception e) {
            log.error("同步任务失败 taskNo={}", task.getTaskNo(), e);
            syncTaskService.finishTask(task.getId(), SyncStatus.FAILED.getCode(), e.getMessage());
        } finally {
            runningFlags.remove(key);
        }
    }

    private void doFullSync(SyncTask task, ShardRule rule, AtomicLong success, AtomicLong fail) {
        int batchSize = 500;
        Long lastId = 0L;
        String logicTable = rule.getLogicTable();
        String pk = rule.getPrimaryKey();
        while (true) {
            if (isCanceled(task)) {
                log.info("任务被取消 taskNo={}, table={}", task.getTaskNo(), logicTable);
                break;
            }
            List<Map<String, Object>> rows;
            try {
                rows = jdbcTemplate.queryForList(
                        "SELECT * FROM " + logicTable + " WHERE " + pk + " > ? ORDER BY " + pk + " LIMIT ?",
                        lastId, batchSize);
            } catch (Exception e) {
                log.error("全量同步查询失败 table={}: {}", logicTable, e.getMessage());
                break;
            }
            if (rows.isEmpty()) {
                break;
            }
            for (Map<String, Object> row : rows) {
                try {
                    upsertRow(logicTable, row, pk);
                    success.incrementAndGet();
                } catch (Exception e) {
                    fail.incrementAndGet();
                    log.warn("行同步失败 table={} pk={} err={}", logicTable, row.get(pk), e.getMessage());
                }
                Object pkObj = row.get(pk);
                if (pkObj instanceof Number) {
                    lastId = ((Number) pkObj).longValue();
                }
            }
            syncTaskService.updateProgress(task.getId(), success.get(), fail.get(), null);
            if (rows.size() < batchSize) {
                break;
            }
        }
    }

    private void doIncrementalSync(SyncTask task, ShardRule rule, AtomicLong success, AtomicLong fail) {
        String logicTable = rule.getLogicTable();
        String pk = rule.getPrimaryKey();
        Map<String, Object> params = parseParams(task.getParams());
        Object since = params.get("since");
        Object until = params.get("until");
        StringBuilder sql = new StringBuilder("SELECT * FROM ").append(logicTable).append(" WHERE 1=1");
        List<Object> args = new ArrayList<>();
        if (since != null) {
            sql.append(" AND update_time >= ?");
            args.add(since);
        }
        if (until != null) {
            sql.append(" AND update_time <= ?");
            args.add(until);
        }
        sql.append(" ORDER BY ").append(pk);
        List<Map<String, Object>> rows;
        try {
            rows = jdbcTemplate.queryForList(sql.toString(), args.toArray());
        } catch (Exception e) {
            log.warn("增量查询失败, 降级全量同步: {}", e.getMessage());
            doFullSync(task, rule, success, fail);
            return;
        }
        for (Map<String, Object> row : rows) {
            if (isCanceled(task)) {
                break;
            }
            try {
                upsertRow(logicTable, row, pk);
                success.incrementAndGet();
            } catch (Exception e) {
                fail.incrementAndGet();
            }
        }
    }

    private void doBinlogSync(SyncTask task, ShardRule rule, AtomicLong success, AtomicLong fail) {
        String logicTable = rule.getLogicTable();
        String pk = rule.getPrimaryKey();
        Map<String, Object> params = parseParams(task.getParams());
        List<Map<String, Object>> events = (List<Map<String, Object>>) params.get("events");
        if (events == null || events.isEmpty()) {
            log.info("无 Binlog 事件 table={}", logicTable);
            return;
        }
        List<Long> failedIds = lastBinlogFailedEventIds.get();
        for (Map<String, Object> event : events) {
            if (isCanceled(task)) {
                log.info("Binlog 任务被取消, 剩余 {} 条事件未处理", events.size());
                break;
            }
            Long eventId = event.get("eventId") == null ? null : ((Number) event.get("eventId")).longValue();
            try {
                String action = String.valueOf(event.get("action"));
                Map<String, Object> row = (Map<String, Object>) event.get("row");
                if (row == null || row.isEmpty()) {
                    if (eventId != null) {
                        failedIds.add(eventId);
                    }
                    fail.incrementAndGet();
                    continue;
                }
                if ("DELETE".equalsIgnoreCase(action)) {
                    Object pkVal = row.get(pk);
                    if (pkVal == null) {
                        if (eventId != null) {
                            failedIds.add(eventId);
                        }
                        fail.incrementAndGet();
                        continue;
                    }
                    jdbcTemplate.update("DELETE FROM " + logicTable + " WHERE " + pk + " = ?", pkVal);
                } else {
                    upsertRow(logicTable, row, pk);
                }
                success.incrementAndGet();
            } catch (Exception e) {
                fail.incrementAndGet();
                if (eventId != null) {
                    failedIds.add(eventId);
                }
                log.warn("Binlog 事件处理失败 table={} action={} err={}",
                        logicTable, event.get("action"), e.getMessage());
            }
        }
    }

    private void upsertRow(String logicTable, Map<String, Object> row, String pk) {
        if (row == null || row.isEmpty()) {
            return;
        }
        List<String> columns = new ArrayList<>(row.keySet());
        String columnsExpr = String.join(",", columns);
        String placeholders = columns.stream().map(c -> "?").reduce((a, b) -> a + "," + b).orElse("");
        List<String> updateExpr = new ArrayList<>();
        for (String col : columns) {
            if (!col.equalsIgnoreCase(pk)) {
                updateExpr.add(col + " = VALUES(" + col + ")");
            }
        }
        String sql = "INSERT INTO " + logicTable + " (" + columnsExpr + ") VALUES (" + placeholders + ")";
        if (!updateExpr.isEmpty()) {
            sql += " ON DUPLICATE KEY UPDATE " + String.join(",", updateExpr);
        }
        Object[] args = columns.stream().map(row::get).toArray();
        jdbcTemplate.update(sql, args);
    }

    private boolean isCanceled(SyncTask task) {
        SyncTask latest = syncTaskService.getById(task.getId());
        return latest != null && SyncStatus.CANCELED.getCode().equals(latest.getStatus());
    }

    private Map<String, Object> parseParams(String json) {
        if (json == null || json.isEmpty()) {
            return new HashMap<>();
        }
        try {
            return JSON.parseObject(json, Map.class);
        } catch (Exception e) {
            return new HashMap<>();
        }
    }
}
