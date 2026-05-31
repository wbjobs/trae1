package com.sharding.sync.check.service.impl;

import com.alibaba.fastjson2.JSON;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.sharding.sync.check.entity.CheckDiff;
import com.sharding.sync.check.entity.CheckTask;
import com.sharding.sync.check.mapper.CheckDiffMapper;
import com.sharding.sync.check.mapper.CheckTaskMapper;
import com.sharding.sync.check.service.CheckService;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.shard.algorithm.ShardAlgorithmRegistry;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.Executor;

@Slf4j
@Service
@RequiredArgsConstructor
public class CheckServiceImpl extends ServiceImpl<CheckTaskMapper, CheckTask> implements CheckService {

    private final CheckDiffMapper checkDiffMapper;
    private final ShardRuleService shardRuleService;
    private final ShardAlgorithmRegistry shardAlgorithmRegistry;
    private final JdbcTemplate jdbcTemplate;
    private final DataSource dataSource;
    @Qualifier("checkTaskExecutor")
    private final Executor checkTaskExecutor;

    @Override
    public CheckTask submit(String logicTable, String checkType) {
        CheckTask task = new CheckTask();
        task.setTaskNo(UUID.randomUUID().toString().replace("-", ""));
        task.setLogicTable(logicTable);
        task.setCheckType(checkType);
        task.setStatus(SyncStatus.PENDING.getCode());
        task.setTotalCount(0L);
        task.setDiffCount(0L);
        task.setCreateTime(LocalDateTime.now());
        task.setUpdateTime(LocalDateTime.now());
        save(task);
        checkTaskExecutor.execute(() -> runCheck(task));
        return task;
    }

    @Override
    public CheckTask getByTaskNo(String taskNo) {
        return getOne(new LambdaQueryWrapper<CheckTask>().eq(CheckTask::getTaskNo, taskNo));
    }

    @Override
    public IPage<CheckTask> page(Page<CheckTask> page, String logicTable, String status) {
        LambdaQueryWrapper<CheckTask> wrapper = new LambdaQueryWrapper<>();
        if (StringUtils.hasText(logicTable)) {
            wrapper.eq(CheckTask::getLogicTable, logicTable);
        }
        if (StringUtils.hasText(status)) {
            wrapper.eq(CheckTask::getStatus, status);
        }
        wrapper.orderByDesc(CheckTask::getCreateTime);
        return page(page, wrapper);
    }

    @Override
    public List<CheckDiff> listDiffs(Long taskId) {
        return checkDiffMapper.selectList(
                new LambdaQueryWrapper<CheckDiff>().eq(CheckDiff::getTaskId, taskId));
    }

    @Override
    public List<CheckDiff> listPendingFixes(String logicTable, int limit) {
        LambdaQueryWrapper<CheckDiff> wrapper = new LambdaQueryWrapper<>();
        wrapper.eq(CheckDiff::getLogicTable, logicTable)
                .and(w -> w.isNull(CheckDiff::getFixStatus).or().eq(CheckDiff::getFixStatus, 0))
                .orderByAsc(CheckDiff::getId)
                .last("limit " + limit);
        return checkDiffMapper.selectList(wrapper);
    }

    @Override
    public Map<String, Object> getStatus(String taskNo) {
        CheckTask task = getByTaskNo(taskNo);
        if (task == null) {
            return null;
        }
        Map<String, Object> m = new HashMap<>();
        m.put("taskNo", task.getTaskNo());
        m.put("logicTable", task.getLogicTable());
        m.put("checkType", task.getCheckType());
        m.put("status", task.getStatus());
        m.put("totalCount", task.getTotalCount());
        m.put("diffCount", task.getDiffCount());
        m.put("startTime", task.getStartTime());
        m.put("endTime", task.getEndTime());
        m.put("errorMsg", task.getErrorMsg());
        return m;
    }

    @Override
    public void runCheck(CheckTask task) {
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

            long total = 0L;
            long diff = 0L;
            if ("COUNT".equalsIgnoreCase(task.getCheckType())) {
                diff = checkByCount(rule, task.getId());
            } else if ("CRC".equalsIgnoreCase(task.getCheckType())) {
                diff = checkByCrc(rule, task.getId());
            } else if ("ROW".equalsIgnoreCase(task.getCheckType())) {
                diff = checkByRow(rule, task.getId());
            } else {
                diff = checkByCount(rule, task.getId()) + checkByCrc(rule, task.getId());
            }
            task.setTotalCount(total);
            task.setDiffCount(diff);
            task.setStatus(diff == 0 ? SyncStatus.SUCCESS.getCode() : SyncStatus.PARTIAL.getCode());
            task.setEndTime(LocalDateTime.now());
            task.setUpdateTime(LocalDateTime.now());
            updateById(task);
        } catch (Exception e) {
            log.error("校验任务失败 taskNo={}", task.getTaskNo(), e);
            task.setStatus(SyncStatus.FAILED.getCode());
            task.setErrorMsg(e.getMessage());
            task.setEndTime(LocalDateTime.now());
            updateById(task);
        }
    }

    private long checkByCount(ShardRule rule, Long taskId) {
        Map<String, Long> counts = countPerShard(rule);
        if (counts.size() <= 1) {
            return 0;
        }
        Set<Long> values = new HashSet<>(counts.values());
        long diffCount = 0;
        if (values.size() > 1) {
            for (Map.Entry<String, Long> a : counts.entrySet()) {
                for (Map.Entry<String, Long> b : counts.entrySet()) {
                    if (a.getKey().compareTo(b.getKey()) >= 0) {
                        continue;
                    }
                    if (!Objects.equals(a.getValue(), b.getValue())) {
                        diffCount += Math.abs(a.getValue() - b.getValue());
                        CheckDiff diff = new CheckDiff();
                        diff.setTaskId(taskId);
                        diff.setLogicTable(rule.getLogicTable());
                        diff.setDiffType("COUNT_MISMATCH");
                        diff.setShardA(a.getKey());
                        diff.setShardB(b.getKey());
                        diff.setSourceData(String.valueOf(a.getValue()));
                        diff.setTargetData(String.valueOf(b.getValue()));
                        diff.setFixStatus(0);
                        diff.setCreateTime(LocalDateTime.now());
                        checkDiffMapper.insert(diff);
                    }
                }
            }
        }
        return diffCount;
    }

    private long checkByCrc(ShardRule rule, Long taskId) {
        Map<String, String> crcs = crcPerShard(rule);
        long diffCount = 0;
        Set<String> values = new HashSet<>(crcs.values());
        if (values.size() > 1) {
            for (Map.Entry<String, String> a : crcs.entrySet()) {
                for (Map.Entry<String, String> b : crcs.entrySet()) {
                    if (a.getKey().compareTo(b.getKey()) >= 0) {
                        continue;
                    }
                    if (!Objects.equals(a.getValue(), b.getValue())) {
                        diffCount++;
                        CheckDiff diff = new CheckDiff();
                        diff.setTaskId(taskId);
                        diff.setLogicTable(rule.getLogicTable());
                        diff.setDiffType("CRC_MISMATCH");
                        diff.setShardA(a.getKey());
                        diff.setShardB(b.getKey());
                        diff.setSourceData(a.getValue());
                        diff.setTargetData(b.getValue());
                        diff.setFixStatus(0);
                        diff.setCreateTime(LocalDateTime.now());
                        checkDiffMapper.insert(diff);
                    }
                }
            }
        }
        return diffCount;
    }

    private long checkByRow(ShardRule rule, Long taskId) {
        long diffCount = 0;
        String logicTable = rule.getLogicTable();
        String pk = rule.getPrimaryKey();
        List<Map<String, Object>> allRows = jdbcTemplate.queryForList("SELECT * FROM " + logicTable);
        Map<String, List<Map<String, Object>>> byShard = new HashMap<>();
        for (Map<String, Object> row : allRows) {
            String shard = resolveShard(rule, row.get(rule.getShardingColumn()));
            byShard.computeIfAbsent(shard, k -> new ArrayList<>()).add(row);
        }
        Map<String, Map<Object, Map<String, Object>>> byKey = new HashMap<>();
        for (Map.Entry<String, List<Map<String, Object>>> e : byShard.entrySet()) {
            Map<Object, Map<String, Object>> m = new HashMap<>();
            for (Map<String, Object> r : e.getValue()) {
                m.put(r.get(pk), r);
            }
            byKey.put(e.getKey(), m);
        }
        List<String> shards = new ArrayList<>(byKey.keySet());
        for (int i = 0; i < shards.size(); i++) {
            for (int j = i + 1; j < shards.size(); j++) {
                Map<Object, Map<String, Object>> a = byKey.get(shards.get(i));
                Map<Object, Map<String, Object>> b = byKey.get(shards.get(j));
                Set<Object> all = new HashSet<>(a.keySet());
                all.addAll(b.keySet());
                for (Object k : all) {
                    Map<String, Object> ra = a.get(k);
                    Map<String, Object> rb = b.get(k);
                    if (!Objects.equals(ra, rb)) {
                        diffCount++;
                        CheckDiff diff = new CheckDiff();
                        diff.setTaskId(taskId);
                        diff.setLogicTable(logicTable);
                        diff.setPkValue(String.valueOf(k));
                        diff.setDiffType(ra == null ? "MISSING_IN_A" : rb == null ? "MISSING_IN_B" : "ROW_MISMATCH");
                        diff.setShardA(shards.get(i));
                        diff.setShardB(shards.get(j));
                        diff.setSourceData(JSON.toJSONString(ra));
                        diff.setTargetData(JSON.toJSONString(rb));
                        diff.setFixStatus(0);
                        diff.setCreateTime(LocalDateTime.now());
                        checkDiffMapper.insert(diff);
                    }
                }
            }
        }
        return diffCount;
    }

    private Map<String, Long> countPerShard(ShardRule rule) {
        Map<String, Long> result = new HashMap<>();
        if (dataSource instanceof com.sharding.sync.config.DynamicDataSource) {
            com.sharding.sync.config.DynamicDataSource ds = (com.sharding.sync.config.DynamicDataSource) dataSource;
            for (String shard : resolveShardList(rule)) {
                if (!ds.hasDataSource(shard)) {
                    log.warn("分片数据源不存在, 跳过计数 shard={}", shard);
                    continue;
                }
                try (Connection c = ds.getConnection(shard);
                     Statement st = c.createStatement();
                     ResultSet rs = st.executeQuery("SELECT COUNT(*) AS cnt FROM " + rule.getLogicTable())) {
                    if (rs.next()) {
                        result.put(shard, rs.getLong(1));
                    }
                } catch (Exception e) {
                    log.warn("分片计数失败 shard={}: {}", shard, e.getMessage());
                }
            }
        } else {
            try {
                Long cnt = jdbcTemplate.queryForObject("SELECT COUNT(*) FROM " + rule.getLogicTable(), Long.class);
                result.put("all", cnt == null ? 0 : cnt);
            } catch (Exception e) {
                log.warn("查询失败: {}", e.getMessage());
            }
        }
        return result;
    }

    private Map<String, String> crcPerShard(ShardRule rule) {
        Map<String, String> result = new HashMap<>();
        if (dataSource instanceof com.sharding.sync.config.DynamicDataSource) {
            com.sharding.sync.config.DynamicDataSource ds = (com.sharding.sync.config.DynamicDataSource) dataSource;
            for (String shard : resolveShardList(rule)) {
                if (!ds.hasDataSource(shard)) {
                    log.warn("分片数据源不存在, 跳过CRC shard={}", shard);
                    continue;
                }
                try (Connection c = ds.getConnection(shard);
                     Statement st = c.createStatement();
                     ResultSet rs = st.executeQuery(
                             "SELECT MD5(GROUP_CONCAT(CONCAT_WS('#'," + rule.getPrimaryKey() + "))) AS sig FROM " + rule.getLogicTable())) {
                    if (rs.next()) {
                        result.put(shard, rs.getString(1));
                    }
                } catch (Exception e) {
                    log.warn("分片 CRC 计算失败 shard={}: {}", shard, e.getMessage());
                }
            }
        }
        return result;
    }

    private List<String> resolveShardList(ShardRule rule) {
        List<String> list = new ArrayList<>();
        if (rule.getShardNodes() != null && !rule.getShardNodes().isEmpty()) {
            for (String s : rule.getShardNodes().split(",")) {
                if (!s.trim().isEmpty()) {
                    list.add(s.trim());
                }
            }
        }
        if (list.isEmpty()) {
            for (int i = 0; i < rule.getShardCount(); i++) {
                String key = "physical" + i;
                if (dataSource instanceof com.sharding.sync.config.DynamicDataSource) {
                    if (((com.sharding.sync.config.DynamicDataSource) dataSource).hasDataSource(key)) {
                        list.add(key);
                    }
                } else {
                    list.add(key);
                }
            }
        }
        return list;
    }

    private String resolveShard(ShardRule rule, Object shardValue) {
        List<String> list = resolveShardList(rule);
        int idx = 0;
        try {
            com.sharding.sync.shard.algorithm.ShardAlgorithm algo =
                    shardAlgorithmRegistry.get(rule.getAlgorithm());
            idx = algo.shard(shardValue, list.size());
        } catch (Exception ignore) {
        }
        if (list.isEmpty()) {
            return "master";
        }
        return list.get(Math.max(0, Math.min(idx, list.size() - 1)));
    }
}
