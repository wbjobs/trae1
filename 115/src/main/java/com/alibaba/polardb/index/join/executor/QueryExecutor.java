package com.alibaba.polardb.index.join.executor;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.join.cache.QueryCache;
import com.alibaba.polardb.index.join.engine.HashJoinEngine;
import com.alibaba.polardb.index.join.engine.NestedLoopJoinEngine;
import com.alibaba.polardb.index.join.model.*;
import com.alibaba.polardb.index.join.optimizer.PredicatePushDownOptimizer;
import com.alibaba.polardb.index.join.optimizer.QueryOptimizer;
import com.alibaba.polardb.index.join.stats.JoinStatisticsManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowCallbackHandler;
import org.springframework.stereotype.Component;

import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

@Component
public class QueryExecutor {
    private static final Logger logger = LoggerFactory.getLogger(QueryExecutor.class);

    @Autowired
    private QueryOptimizer queryOptimizer;

    @Autowired
    private QueryCache queryCache;

    @Autowired
    private HashJoinEngine hashJoinEngine;

    @Autowired
    private NestedLoopJoinEngine nestedLoopJoinEngine;

    @Autowired
    private PredicatePushDownOptimizer predicatePushDownOptimizer;

    @Autowired
    private JoinStatisticsManager statisticsManager;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired(required = false)
    private Map<String, JdbcTemplate> shardJdbcTemplates;

    private final ExecutorService executorService = new ThreadPoolExecutor(
            8, 32, 60L, TimeUnit.SECONDS,
            new LinkedBlockingQueue<>(1000),
            r -> {
                Thread t = new Thread(r);
                t.setName("join-executor-" + t.getId());
                t.setDaemon(true);
                return t;
            },
            new ThreadPoolExecutor.CallerRunsPolicy()
    );

    public JoinResult execute(JoinQuery query) throws Exception {
        String queryId = query.getQueryId() != null
                ? query.getQueryId()
                : UUID.randomUUID().toString();
        query.setQueryId(queryId);

        if (query.getTimeoutMs() <= 0) {
            query.setTimeoutMs(getDefaultTimeout());
        }

        long startTime = System.currentTimeMillis();
        JoinResult result;

        JoinResult cached = queryCache.get(query);
        if (cached != null && !query.isExplain()) {
            cached.setExecutionTimeMs(System.currentTimeMillis() - startTime);
            cached.setQueryId(queryId);
            logger.debug("Return cached result for query: {}", queryId);
            return cached;
        }

        QueryPlan plan = queryOptimizer.optimize(query);

        if (query.isExplain()) {
            result = JoinResult.builder()
                    .queryId(queryId)
                    .success(true)
                    .executionPlan(plan)
                    .executionTimeMs(System.currentTimeMillis() - startTime)
                    .rows(Collections.singletonList(
                            Collections.singletonMap("explain", plan.toExplainString())))
                    .totalRows(1)
                    .build();
            return result;
        }

        try {
            result = executePlan(query, plan);
            result.setExecutionPlan(plan);
            result.setExecutionTimeMs(System.currentTimeMillis() - startTime);

            if (result.isSuccess()) {
                queryCache.put(query, result);
            }

        } catch (TimeoutException e) {
            result = JoinResult.builder()
                    .queryId(queryId)
                    .success(false)
                    .errorMessage("Query timeout after " + query.getTimeoutMs() + "ms")
                    .executionPlan(plan)
                    .executionTimeMs(System.currentTimeMillis() - startTime)
                    .build();
        } catch (Exception e) {
            logger.error("Query execution failed: {}", e.getMessage(), e);
            result = JoinResult.builder()
                    .queryId(queryId)
                    .success(false)
                    .errorMessage(e.getMessage())
                    .executionPlan(plan)
                    .executionTimeMs(System.currentTimeMillis() - startTime)
                    .build();
        }

        return result;
    }

    private JoinResult executePlan(JoinQuery query, QueryPlan plan) throws Exception {
        long timeoutMs = query.getTimeoutMs();
        long remainingTime = timeoutMs;
        long startTime = System.currentTimeMillis();

        Map<String, List<Map<String, Object>>> shardResults = executeShardQueries(
                plan, query, remainingTime);

        long shardQueryTime = System.currentTimeMillis() - startTime;
        remainingTime = Math.max(1000, timeoutMs - shardQueryTime);

        if (shardResults.isEmpty()) {
            return JoinResult.builder()
                    .queryId(query.getQueryId())
                    .success(true)
                    .rows(Collections.emptyList())
                    .totalRows(0)
                    .shardQueryTimes(Collections.singletonMap("total", shardQueryTime))
                    .joinTimeMs(0)
                    .shardCount(plan.getShardQueries().size())
                    .build();
        }

        long joinStartTime = System.currentTimeMillis();
        List<Map<String, Object>> joinedResults = performJoin(
                query, plan, shardResults, remainingTime);

        long joinTimeMs = System.currentTimeMillis() - joinStartTime;

        List<Map<String, Object>> finalResults = applyLocalFilters(query, joinedResults);

        if (query.getOrderByColumns() != null && !query.getOrderByColumns().isEmpty()) {
            finalResults = sortResults(finalResults, query.getOrderByColumns());
        }

        if (query.getLimit() != null) {
            finalResults = applyLimit(finalResults, query.getOffset(), query.getLimit());
        }

        Map<String, Long> shardTimes = new HashMap<>();
        shardTimes.put("total", shardQueryTime);
        shardTimes.put("join", joinTimeMs);

        return JoinResult.builder()
                .queryId(query.getQueryId())
                .success(true)
                .rows(finalResults)
                .totalRows(finalResults.size())
                .shardQueryTimes(shardTimes)
                .joinTimeMs(joinTimeMs)
                .shardCount(plan.getShardQueries().size())
                .build();
    }

    private Map<String, List<Map<String, Object>>> executeShardQueries(
            QueryPlan plan, JoinQuery query, long timeoutMs) throws Exception {

        Map<String, List<Map<String, Object>>> allResults = new ConcurrentHashMap<>();
        Map<String, Long> shardQueryTimes = new ConcurrentHashMap<>();
        AtomicBoolean hasError = new AtomicBoolean(false);
        AtomicLong totalFetched = new AtomicLong(0);

        List<Callable<Void>> tasks = new ArrayList<>();

        for (Map.Entry<String, List<String>> entry : plan.getShardQueries().entrySet()) {
            String shardId = entry.getKey();
            List<String> sqls = entry.getValue();
            List<JoinTable> tables = query.getTables();

            tasks.add(() -> {
                if (hasError.get()) return null;

                long shardStart = System.currentTimeMillis();
                try {
                    JdbcTemplate jdbcTemplate = getShardJdbcTemplate(shardId);
                    if (jdbcTemplate == null) {
                        logger.warn("No JDBC template for shard: {}, skipping", shardId);
                        return null;
                    }

                    List<Map<String, Object>> tableResults = new ArrayList<>();
                    for (int i = 0; i < sqls.size(); i++) {
                        String sql = sqls.get(i);
                        JoinTable table = tables.get(i);

                        List<Map<String, Object>> rows = executeShardQuery(
                                jdbcTemplate, shardId, sql, table,
                                timeoutMs / sqls.size());
                        tableResults.addAll(rows);
                    }

                    shardQueryTimes.put(shardId, System.currentTimeMillis() - shardStart);
                    statisticsManager.recordQueryLatency(shardId, shardQueryTimes.get(shardId));

                    if (!tableResults.isEmpty()) {
                        allResults.put(shardId, tableResults);
                        totalFetched.addAndGet(tableResults.size());
                    }

                } catch (Exception e) {
                    logger.error("Failed to execute query on shard {}: {}",
                            shardId, e.getMessage());
                    hasError.set(true);
                    throw e;
                }
                return null;
            });
        }

        List<Future<Void>> futures = executorService.invokeAll(tasks, timeoutMs, TimeUnit.MILLISECONDS);

        for (Future<Void> future : futures) {
            try {
                future.get(1, TimeUnit.MILLISECONDS);
            } catch (TimeoutException e) {
                throw new TimeoutException("Shard query timed out");
            } catch (ExecutionException e) {
                Throwable cause = e.getCause();
                if (cause instanceof Exception) {
                    throw (Exception) cause;
                }
                throw new RuntimeException(cause);
            }
        }

        if (hasError.get()) {
            throw new RuntimeException("One or more shard queries failed");
        }

        logger.debug("Fetched {} rows from {} shards", totalFetched.get(), allResults.size());
        return allResults;
    }

    private List<Map<String, Object>> executeShardQuery(
            JdbcTemplate jdbcTemplate, String shardId, String sql,
            JoinTable table, long timeoutMs) {

        List<Map<String, Object>> rows = new ArrayList<>();
        AtomicLong fetched = new AtomicLong(0);

        logger.debug("Executing on shard {}: {}", shardId, sql);

        jdbcTemplate.query(sql, new RowCallbackHandler() {
            @Override
            public void processRow(ResultSet rs) throws SQLException {
                Map<String, Object> row = new LinkedHashMap<>();
                ResultSetMetaData meta = rs.getMetaData();
                int colCount = meta.getColumnCount();

                for (int i = 1; i <= colCount; i++) {
                    String colName = meta.getColumnLabel(i);
                    Object value = rs.getObject(i);
                    row.put(colName, value);
                }

                rows.add(row);
                fetched.incrementAndGet();
            }
        });

        logger.debug("Shard {} returned {} rows", shardId, rows.size());
        return rows;
    }

    private List<Map<String, Object>> performJoin(
            JoinQuery query, QueryPlan plan,
            Map<String, List<Map<String, Object>>> shardResults,
            long timeoutMs) throws Exception {

        if (query.getJoinConditions() == null || query.getJoinConditions().isEmpty()) {
            List<Map<String, Object>> allRows = new ArrayList<>();
            for (List<Map<String, Object>> rows : shardResults.values()) {
                allRows.addAll(rows);
            }
            return allRows;
        }

        long startTime = System.currentTimeMillis();
        List<Map<String, Object>> currentResults = null;

        for (int i = 0; i < query.getJoinConditions().size(); i++) {
            JoinCondition cond = query.getJoinConditions().get(i);

            if (currentResults == null) {
                currentResults = performFirstJoin(query, plan, cond, shardResults);
            } else {
                JoinTable rightTable = findTableByAlias(query, cond.getRightTable());
                List<Map<String, Object>> rightRows = collectTableRows(shardResults, rightTable);

                if ("HASH_JOIN".equals(plan.getJoinAlgorithm())) {
                    currentResults = hashJoinEngine.join(
                            currentResults, rightRows, cond,
                            cond.getLeftTable(), cond.getRightTable());
                } else {
                    currentResults = nestedLoopJoinEngine.join(
                            currentResults, rightRows, cond,
                            cond.getLeftTable(), cond.getRightTable());
                }
            }

            long elapsed = System.currentTimeMillis() - startTime;
            if (elapsed > timeoutMs) {
                throw new TimeoutException("Join timed out after " + elapsed + "ms");
            }
        }

        return currentResults != null ? currentResults : Collections.emptyList();
    }

    private List<Map<String, Object>> performFirstJoin(
            JoinQuery query, QueryPlan plan, JoinCondition cond,
            Map<String, List<Map<String, Object>>> shardResults) {

        JoinTable leftTable = findTableByAlias(query, cond.getLeftTable());
        JoinTable rightTable = findTableByAlias(query, cond.getRightTable());

        List<Map<String, Object>> leftRows = collectTableRows(shardResults, leftTable);
        List<Map<String, Object>> rightRows = collectTableRows(shardResults, rightTable);

        if ("HASH_JOIN".equals(plan.getJoinAlgorithm())) {
            return hashJoinEngine.join(
                    leftRows, rightRows, cond,
                    leftTable.getAlias(), rightTable.getAlias());
        } else {
            return nestedLoopJoinEngine.join(
                    leftRows, rightRows, cond,
                    leftTable.getAlias(), rightTable.getAlias());
        }
    }

    private List<Map<String, Object>> collectTableRows(
            Map<String, List<Map<String, Object>>> shardResults, JoinTable table) {

        List<Map<String, Object>> allRows = new ArrayList<>();
        if (table == null) return allRows;

        for (List<Map<String, Object>> shardRows : shardResults.values()) {
            for (Map<String, Object> row : shardRows) {
                if (table.getTableName() != null
                        && row.containsKey(table.getGlobalIdColumn())) {
                    allRows.add(row);
                } else if (table.getAlias() != null
                        && row.containsKey(table.getAlias() + "." + table.getGlobalIdColumn())) {
                    allRows.add(row);
                } else if (table.getTableName() != null
                        && row.keySet().stream().anyMatch(k -> k.contains(table.getGlobalIdColumn()))) {
                    allRows.add(row);
                }
            }
        }
        return allRows;
    }

    private List<Map<String, Object>> applyLocalFilters(
            JoinQuery query, List<Map<String, Object>> rows) {

        if (rows == null || rows.isEmpty()) {
            return rows;
        }

        List<Predicate> localPredicates = new ArrayList<>();
        if (query.getTables() != null) {
            for (JoinTable table : query.getTables()) {
                if (table.getPredicates() != null) {
                    for (Predicate p : table.getPredicates()) {
                        if (!p.canPushdown()) {
                            localPredicates.add(p);
                        }
                    }
                }
            }
        }

        if (localPredicates.isEmpty()) {
            return rows;
        }

        List<Map<String, Object>> filtered = new ArrayList<>();
        for (Map<String, Object> row : rows) {
            if (predicatePushDownOptimizer.applyLocalFilter(row, localPredicates)) {
                filtered.add(row);
            }
        }

        logger.debug("Local filtering: {} -> {} rows", rows.size(), filtered.size());
        return filtered;
    }

    private List<Map<String, Object>> sortResults(
            List<Map<String, Object>> rows, List<String> orderByColumns) {

        rows.sort((r1, r2) -> {
            for (String col : orderByColumns) {
                boolean descending = col.toUpperCase().contains(" DESC");
                String cleanCol = col.replaceAll("(?i)\\s+(DESC|ASC)", "").trim();

                Object v1 = r1.get(cleanCol);
                Object v2 = r2.get(cleanCol);

                if (v1 == null && v2 == null) continue;
                if (v1 == null) return descending ? 1 : -1;
                if (v2 == null) return descending ? -1 : 1;

                try {
                    @SuppressWarnings("unchecked")
                    Comparable<Object> c1 = (Comparable<Object>) v1;
                    int cmp = c1.compareTo(v2);
                    if (cmp != 0) {
                        return descending ? -cmp : cmp;
                    }
                } catch (ClassCastException e) {
                    int cmp = String.valueOf(v1).compareTo(String.valueOf(v2));
                    if (cmp != 0) {
                        return descending ? -cmp : cmp;
                    }
                }
            }
            return 0;
        });

        return rows;
    }

    private List<Map<String, Object>> applyLimit(
            List<Map<String, Object>> rows, Integer offset, Integer limit) {

        int from = offset != null ? offset : 0;
        int to = Math.min(rows.size(), from + limit);

        if (from >= rows.size()) {
            return Collections.emptyList();
        }

        return rows.subList(from, to);
    }

    private JdbcTemplate getShardJdbcTemplate(String shardId) {
        if (shardJdbcTemplates == null) return null;
        return shardJdbcTemplates.get(shardId);
    }

    private long getDefaultTimeout() {
        if (properties.getJoin() != null
                && properties.getJoin().getTimeoutMs() != null) {
            return properties.getJoin().getTimeoutMs();
        }
        return 30000;
    }

    private JoinTable findTableByAlias(JoinQuery query, String alias) {
        if (query.getTables() == null) return null;
        for (JoinTable table : query.getTables()) {
            if (alias != null && (alias.equals(table.getAlias())
                    || alias.equals(table.getTableName()))) {
                return table;
            }
        }
        return null;
    }

    public void shutdown() {
        executorService.shutdownNow();
    }
}
