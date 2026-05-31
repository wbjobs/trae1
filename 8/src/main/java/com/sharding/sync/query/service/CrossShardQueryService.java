package com.sharding.sync.query.service;

import com.sharding.sync.common.BusinessException;
import com.sharding.sync.config.DynamicDataSource;
import com.sharding.sync.shard.algorithm.ShardAlgorithm;
import com.sharding.sync.shard.algorithm.ShardAlgorithmRegistry;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import javax.sql.DataSource;
import java.sql.*;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Slf4j
@Service
@RequiredArgsConstructor
public class CrossShardQueryService {

    private final DynamicDataSource dynamicDataSource;
    private final ShardRuleService shardRuleService;
    private final ShardAlgorithmRegistry shardAlgorithmRegistry;

    private static final Pattern AGGREGATE_PATTERN =
            Pattern.compile("(?i)\\b(SUM|COUNT|AVG|MIN|MAX|GROUP_CONCAT)\\s*\\(([^)]+)\\)");

    public Map<String, Object> query(String logicTable, String sql, List<Object> params,
                                     Integer page, Integer size) {
        ShardRule rule = shardRuleService.getByLogicTable(logicTable);
        if (rule == null) {
            throw new BusinessException("分片规则不存在: " + logicTable);
        }
        List<String> shards = resolveShardList(rule);
        if (shards.isEmpty()) {
            throw new BusinessException("未找到分片节点: " + logicTable);
        }
        int offset = (page != null && page > 0 ? page - 1 : 0) * (size != null && size > 0 ? size : 100);
        int limit = size != null && size > 0 ? size : 100;
        String pagedSql = sql + " LIMIT " + limit + " OFFSET " + offset;
        List<Map<String, Object>> merged = new ArrayList<>();
        long total = 0;
        Map<String, String> shardErrors = new HashMap<>();
        for (String shard : shards) {
            if (!dynamicDataSource.hasDataSource(shard)) {
                shardErrors.put(shard, "数据源不存在");
                continue;
            }
            try {
                DataSource ds = dynamicDataSource.getDataSource(shard);
                try (Connection c = ds.getConnection();
                     PreparedStatement ps = c.prepareStatement(pagedSql)) {
                    if (params != null) {
                        for (int i = 0; i < params.size(); i++) {
                            ps.setObject(i + 1, params.get(i));
                        }
                    }
                    try (ResultSet rs = ps.executeQuery()) {
                        ResultSetMetaData meta = rs.getMetaData();
                        int colCount = meta.getColumnCount();
                        while (rs.next()) {
                            Map<String, Object> row = new LinkedHashMap<>();
                            for (int i = 1; i <= colCount; i++) {
                                row.put(meta.getColumnLabel(i), rs.getObject(i));
                            }
                            row.put("_shard", shard);
                            merged.add(row);
                        }
                    }
                }
                try (Connection c = ds.getConnection();
                     PreparedStatement ps = c.prepareStatement("SELECT COUNT(*) FROM (" + sql + ") _cnt");
                     ResultSet rs = ps.executeQuery()) {
                    if (rs.next()) {
                        total += rs.getLong(1);
                    }
                }
            } catch (Exception e) {
                log.warn("跨分片查询失败 shard={} sql={} err={}", shard, pagedSql, e.getMessage());
                shardErrors.put(shard, e.getMessage());
            }
        }
        if (merged.isEmpty() && !shardErrors.isEmpty()) {
            log.warn("所有分片查询失败 table={} errors={}", logicTable, shardErrors);
        }
        Map<String, Object> result = new HashMap<>();
        result.put("total", total);
        result.put("data", merged);
        result.put("shards", shards);
        if (!shardErrors.isEmpty()) {
            result.put("errors", shardErrors);
        }
        return result;
    }

    public Map<String, Object> aggregate(String logicTable, String sql, List<Object> params) {
        ShardRule rule = shardRuleService.getByLogicTable(logicTable);
        if (rule == null) {
            throw new BusinessException("分片规则不存在: " + logicTable);
        }
        List<String> shards = resolveShardList(rule);
        if (shards.isEmpty()) {
            throw new BusinessException("未找到分片节点: " + logicTable);
        }
        List<String> aggFunctions = detectAggregateFunctions(sql);
        Map<String, List<Map<String, Object>>> shardResults = new LinkedHashMap<>();
        Map<String, String> shardErrors = new HashMap<>();
        for (String shard : shards) {
            if (!dynamicDataSource.hasDataSource(shard)) {
                shardErrors.put(shard, "数据源不存在");
                continue;
            }
            try {
                DataSource ds = dynamicDataSource.getDataSource(shard);
                try (Connection c = ds.getConnection();
                     PreparedStatement ps = c.prepareStatement(sql)) {
                    if (params != null) {
                        for (int i = 0; i < params.size(); i++) {
                            ps.setObject(i + 1, params.get(i));
                        }
                    }
                    try (ResultSet rs = ps.executeQuery()) {
                        ResultSetMetaData meta = rs.getMetaData();
                        int colCount = meta.getColumnCount();
                        List<Map<String, Object>> rows = new ArrayList<>();
                        while (rs.next()) {
                            Map<String, Object> row = new LinkedHashMap<>();
                            for (int i = 1; i <= colCount; i++) {
                                row.put(meta.getColumnLabel(i), rs.getObject(i));
                            }
                            rows.add(row);
                        }
                        shardResults.put(shard, rows);
                    }
                }
            } catch (Exception e) {
                log.warn("分片聚合查询失败 shard={} sql={} err={}", shard, sql, e.getMessage());
                shardErrors.put(shard, e.getMessage());
            }
        }
        Map<String, Object> merged = mergeAggregateResults(shardResults, aggFunctions);
        Map<String, Object> result = new HashMap<>();
        result.put("data", merged);
        result.put("shards", shards);
        result.put("aggregateFunctions", aggFunctions);
        if (!shardErrors.isEmpty()) {
            result.put("errors", shardErrors);
        }
        return result;
    }

    public Map<String, Object> groupBy(String logicTable, String sql, List<Object> params,
                                       String groupColumn, List<String> aggregateColumns) {
        ShardRule rule = shardRuleService.getByLogicTable(logicTable);
        if (rule == null) {
            throw new BusinessException("分片规则不存在: " + logicTable);
        }
        List<String> shards = resolveShardList(rule);
        if (shards.isEmpty()) {
            throw new BusinessException("未找到分片节点: " + logicTable);
        }
        Map<String, Map<String, Object>> merged = new LinkedHashMap<>();
        Map<String, Long> shardCounts = new HashMap<>();
        Map<String, String> shardErrors = new HashMap<>();
        for (String shard : shards) {
            if (!dynamicDataSource.hasDataSource(shard)) {
                shardErrors.put(shard, "数据源不存在");
                continue;
            }
            try {
                DataSource ds = dynamicDataSource.getDataSource(shard);
                try (Connection c = ds.getConnection();
                     PreparedStatement ps = c.prepareStatement(sql)) {
                    if (params != null) {
                        for (int i = 0; i < params.size(); i++) {
                            ps.setObject(i + 1, params.get(i));
                        }
                    }
                    try (ResultSet rs = ps.executeQuery()) {
                        ResultSetMetaData meta = rs.getMetaData();
                        int colCount = meta.getColumnCount();
                        Map<String, Integer> colIndex = new HashMap<>();
                        for (int i = 1; i <= colCount; i++) {
                            colIndex.put(meta.getColumnLabel(i).toLowerCase(), i);
                        }
                        while (rs.next()) {
                            Object groupKey = rs.getObject(colIndex.getOrDefault(groupColumn.toLowerCase(), 1));
                            String key = String.valueOf(groupKey);
                            Map<String, Object> existing = merged.computeIfAbsent(key, k -> new LinkedHashMap<>());
                            existing.put(groupColumn, groupKey);
                            if (aggregateColumns != null) {
                                for (String aggCol : aggregateColumns) {
                                    int idx = colIndex.getOrDefault(aggCol.toLowerCase(), -1);
                                    if (idx > 0) {
                                        Object val = rs.getObject(idx);
                                        Object prev = existing.get(aggCol);
                                        existing.put(aggCol, mergeValue(prev, val));
                                    }
                                }
                            }
                        }
                    }
                }
                try (Connection c = ds.getConnection();
                     Statement st = c.createStatement();
                     ResultSet rs = st.executeQuery("SELECT COUNT(*) FROM " + logicTable)) {
                    if (rs.next()) {
                        shardCounts.put(shard, rs.getLong(1));
                    }
                }
            } catch (Exception e) {
                log.warn("分片 GROUP BY 查询失败 shard={} err={}", shard, e.getMessage());
                shardErrors.put(shard, e.getMessage());
            }
        }
        Map<String, Object> result = new HashMap<>();
        result.put("data", new ArrayList<>(merged.values()));
        result.put("total", merged.size());
        result.put("shards", shards);
        result.put("shardCounts", shardCounts);
        if (!shardErrors.isEmpty()) {
            result.put("errors", shardErrors);
        }
        return result;
    }

    private Object mergeValue(Object prev, Object curr) {
        if (prev == null) return curr;
        if (curr == null) return prev;
        if (prev instanceof Number && curr instanceof Number) {
            return ((Number) prev).doubleValue() + ((Number) curr).doubleValue();
        }
        return String.valueOf(prev) + "," + String.valueOf(curr);
    }

    List<String> detectAggregateFunctions(String sql) {
        List<String> functions = new ArrayList<>();
        Matcher m = AGGREGATE_PATTERN.matcher(sql);
        while (m.find()) {
            functions.add(m.group(1).toUpperCase() + "(" + m.group(2).trim() + ")");
        }
        return functions;
    }

    private Map<String, Object> mergeAggregateResults(Map<String, List<Map<String, Object>>> shardResults,
                                                      List<String> aggFunctions) {
        Map<String, Object> merged = new LinkedHashMap<>();
        if (shardResults.isEmpty()) {
            return merged;
        }
        for (Map.Entry<String, List<Map<String, Object>>> entry : shardResults.entrySet()) {
            List<Map<String, Object>> rows = entry.getValue();
            for (Map<String, Object> row : rows) {
                for (Map.Entry<String, Object> col : row.entrySet()) {
                    String colName = col.getKey();
                    Object val = col.getValue();
                    Object prev = merged.get(colName);
                    if (prev == null) {
                        merged.put(colName, val);
                    } else {
                        merged.put(colName, mergeValue(prev, val));
                    }
                }
            }
        }
        return merged;
    }

    public Map<String, Object> queryByShardColumn(String logicTable, Object shardValue, String sql, List<Object> params) {
        ShardRule rule = shardRuleService.getByLogicTable(logicTable);
        if (rule == null) {
            throw new BusinessException("分片规则不存在: " + logicTable);
        }
        ShardAlgorithm algo = shardAlgorithmRegistry.get(rule.getAlgorithm());
        List<String> shards = resolveShardList(rule);
        if (shards.isEmpty()) {
            throw new BusinessException("未找到分片节点: " + logicTable);
        }
        int idx = algo.shard(shardValue, shards.size());
        String shard = shards.get(Math.max(0, Math.min(idx, shards.size() - 1)));
        if (!dynamicDataSource.hasDataSource(shard)) {
            throw new BusinessException("数据源不存在: " + shard);
        }
        List<Map<String, Object>> rows = new ArrayList<>();
        try {
            DataSource ds = dynamicDataSource.getDataSource(shard);
            try (Connection c = ds.getConnection();
                 PreparedStatement ps = c.prepareStatement(sql)) {
                if (params != null) {
                    for (int i = 0; i < params.size(); i++) {
                        ps.setObject(i + 1, params.get(i));
                    }
                }
                try (ResultSet rs = ps.executeQuery()) {
                    ResultSetMetaData meta = rs.getMetaData();
                    int colCount = meta.getColumnCount();
                    while (rs.next()) {
                        Map<String, Object> row = new LinkedHashMap<>();
                        for (int i = 1; i <= colCount; i++) {
                            row.put(meta.getColumnLabel(i), rs.getObject(i));
                        }
                        rows.add(row);
                    }
                }
            }
        } catch (Exception e) {
            throw new BusinessException("分片查询失败 shard=" + shard + ": " + e.getMessage());
        }
        Map<String, Object> result = new HashMap<>();
        result.put("shard", shard);
        result.put("data", rows);
        result.put("total", rows.size());
        return result;
    }

    public Map<String, Map<String, Object>> queryAllShardsStats(String logicTable) {
        ShardRule rule = shardRuleService.getByLogicTable(logicTable);
        if (rule == null) {
            throw new BusinessException("分片规则不存在: " + logicTable);
        }
        List<String> shards = resolveShardList(rule);
        Map<String, Map<String, Object>> stats = new LinkedHashMap<>();
        for (String shard : shards) {
            Map<String, Object> m = new HashMap<>();
            if (!dynamicDataSource.hasDataSource(shard)) {
                m.put("error", "数据源不存在");
                stats.put(shard, m);
                continue;
            }
            try {
                DataSource ds = dynamicDataSource.getDataSource(shard);
                try (Connection c = ds.getConnection();
                     Statement st = c.createStatement()) {
                    try (ResultSet rs = st.executeQuery("SELECT COUNT(*) AS cnt FROM " + logicTable)) {
                        if (rs.next()) {
                            m.put("count", rs.getLong(1));
                        }
                    }
                    try (ResultSet rs = st.executeQuery("SELECT MAX(" + rule.getPrimaryKey() + ") AS max_id FROM " + logicTable)) {
                        if (rs.next()) {
                            m.put("maxId", rs.getObject(1));
                        }
                    }
                    try (ResultSet rs = st.executeQuery("SELECT MIN(" + rule.getPrimaryKey() + ") AS min_id FROM " + logicTable)) {
                        if (rs.next()) {
                            m.put("minId", rs.getObject(1));
                        }
                    }
                }
                m.put("available", true);
            } catch (Exception e) {
                m.put("available", false);
                m.put("error", e.getMessage());
            }
            stats.put(shard, m);
        }
        return stats;
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
                if (dynamicDataSource.hasDataSource(key)) {
                    list.add(key);
                }
            }
        }
        return list;
    }
}
