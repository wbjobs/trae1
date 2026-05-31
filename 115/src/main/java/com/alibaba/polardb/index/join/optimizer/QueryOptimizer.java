package com.alibaba.polardb.index.join.optimizer;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.join.engine.HashJoinEngine;
import com.alibaba.polardb.index.join.engine.NestedLoopJoinEngine;
import com.alibaba.polardb.index.join.model.*;
import com.alibaba.polardb.index.join.stats.JoinStatisticsManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import java.util.*;

@Component
public class QueryOptimizer {
    private static final Logger logger = LoggerFactory.getLogger(QueryOptimizer.class);

    private static final long HASH_JOIN_THRESHOLD = 1000;
    private static final double COST_WEIGHT_NETWORK = 10.0;
    private static final double COST_WEIGHT_CPU = 1.0;
    private static final double COST_WEIGHT_MEMORY = 0.5;

    @Autowired
    private PredicatePushDownOptimizer predicatePushDownOptimizer;

    @Autowired
    private JoinStatisticsManager statisticsManager;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    private HashJoinEngine hashJoinEngine;

    @Autowired
    private NestedLoopJoinEngine nestedLoopJoinEngine;

    public QueryPlan optimize(JoinQuery query) {
        String planId = UUID.randomUUID().toString();

        JoinQuery optimizedQuery = predicatePushDownOptimizer.optimize(query);

        QueryPlan plan = QueryPlan.builder()
                .planId(planId)
                .originalQuery(optimizedQuery)
                .usePredicatePushdown(true)
                .useResultCache(true)
                .steps(new ArrayList<>())
                .build();

        Set<String> shardIds = getShardIds();
        plan.setShardCount(shardIds.size());

        JoinStrategy strategy = determineJoinStrategy(optimizedQuery);
        plan.setJoinStrategy(strategy);

        String algorithm = chooseJoinAlgorithm(optimizedQuery, shardIds);
        plan.setJoinAlgorithm(algorithm);

        Map<String, List<String>> shardQueries = generateShardQueries(optimizedQuery, shardIds);
        plan.setShardQueries(shardQueries);

        Map<String, Long> shardEstimatedRows = estimateShardRows(optimizedQuery, shardIds);
        plan.setShardEstimatedRows(shardEstimatedRows);

        double totalCost = calculateTotalCost(plan, optimizedQuery, shardIds, shardEstimatedRows);
        plan.setEstimatedCost(totalCost);

        long totalRows = estimateTotalResultRows(optimizedQuery, shardIds, shardEstimatedRows);
        plan.setEstimatedRows(totalRows);

        long estimatedTime = estimateExecutionTime(plan, optimizedQuery, shardIds);
        plan.setEstimatedTimeMs(estimatedTime);

        buildExecutionSteps(plan, optimizedQuery, shardIds);

        logger.info("Optimized query plan generated: strategy={}, algorithm={}, cost={:.2f}, rows={}",
                strategy, algorithm, totalCost, totalRows);

        return plan;
    }

    private Set<String> getShardIds() {
        if (properties.getShards() != null && !properties.getShards().isEmpty()) {
            Set<String> ids = new HashSet<>();
            properties.getShards().forEach(s -> ids.add(s.getShardId()));
            return ids;
        }
        return statisticsManager.getKnownShards();
    }

    private JoinStrategy determineJoinStrategy(JoinQuery query) {
        if (query.getTables() == null || query.getTables().size() < 2) {
            return JoinStrategy.LOOKUP;
        }

        long totalEstimatedRows = 0;
        for (JoinTable table : query.getTables()) {
            for (String shardId : getShardIds()) {
                totalEstimatedRows += statisticsManager.estimateRows(
                        shardId, table.getTableName(),
                        table.getShardKeyColumn(), "=", null);
            }
        }

        if (totalEstimatedRows < 10000) {
            return JoinStrategy.BROADCAST;
        }

        boolean hasGlobalIdJoin = false;
        if (query.getJoinConditions() != null) {
            for (JoinCondition cond : query.getJoinConditions()) {
                if ("global_id".equals(cond.getLeftColumn())
                        || "global_id".equals(cond.getRightColumn())) {
                    hasGlobalIdJoin = true;
                    break;
                }
            }
        }

        return hasGlobalIdJoin ? JoinStrategy.PARTITIONED : JoinStrategy.BROADCAST;
    }

    private String chooseJoinAlgorithm(JoinQuery query, Set<String> shardIds) {
        if (query.getJoinConditions() == null || query.getJoinConditions().isEmpty()) {
            return "NESTED_LOOP";
        }

        long totalRows = 0;
        for (JoinTable table : query.getTables()) {
            for (String shardId : shardIds) {
                totalRows += statisticsManager.estimateRows(
                        shardId, table.getTableName(),
                        table.getShardKeyColumn(), "=", null);
            }
        }

        if (totalRows > HASH_JOIN_THRESHOLD) {
            return "HASH_JOIN";
        } else {
            return "NESTED_LOOP";
        }
    }

    private Map<String, List<String>> generateShardQueries(JoinQuery query, Set<String> shardIds) {
        Map<String, List<String>> shardQueries = new HashMap<>();

        if (query.getTables() == null) {
            return shardQueries;
        }

        for (String shardId : shardIds) {
            List<String> queries = new ArrayList<>();

            for (JoinTable table : query.getTables()) {
                String whereClause = predicatePushDownOptimizer.buildShardWhereClause(table);
                String selectClause = table.getSelectClause();
                String sql = String.format("SELECT %s FROM %s%s",
                        selectClause, table.getFullTableName(), whereClause);

                if (query.getLimit() != null) {
                    sql += " LIMIT " + query.getLimit();
                    if (query.getOffset() != null) {
                        sql += " OFFSET " + query.getOffset();
                    }
                }

                queries.add(sql);
            }

            shardQueries.put(shardId, queries);
        }

        return shardQueries;
    }

    private Map<String, Long> estimateShardRows(JoinQuery query, Set<String> shardIds) {
        Map<String, Long> estimates = new HashMap<>();

        if (query.getTables() == null) {
            return estimates;
        }

        for (String shardId : shardIds) {
            long totalRows = 0;
            for (JoinTable table : query.getTables()) {
                long tableRows = statisticsManager.estimateRows(
                        shardId, table.getTableName(),
                        table.getShardKeyColumn(), "=", null);

                if (table.getPredicates() != null) {
                    for (Predicate p : table.getPredicates()) {
                        if (p.canPushdown()) {
                            double selectivity = statisticsManager.getShardStatistics(shardId)
                                    .getSelectivity(table.getTableName(),
                                            p.getColumn(), p.getOperator(), p.getValue());
                            tableRows = (long) (tableRows * selectivity);
                        }
                    }
                }

                totalRows += tableRows;
            }
            estimates.put(shardId, totalRows);
        }

        return estimates;
    }

    private double calculateTotalCost(QueryPlan plan, JoinQuery query,
                                       Set<String> shardIds,
                                       Map<String, Long> shardEstimatedRows) {
        double totalCost = 0;

        for (Map.Entry<String, Long> entry : shardEstimatedRows.entrySet()) {
            String shardId = entry.getKey();
            long rows = entry.getValue();

            double shardCost = statisticsManager.estimateQueryCost(shardId, rows);
            totalCost += shardCost * COST_WEIGHT_NETWORK;
        }

        long totalRows = shardEstimatedRows.values().stream().mapToLong(Long::longValue).sum();

        if (query.getJoinConditions() != null) {
            for (JoinCondition cond : query.getJoinConditions()) {
                if ("HASH_JOIN".equals(plan.getJoinAlgorithm())) {
                    totalCost += totalRows * 0.1 * COST_WEIGHT_CPU;
                } else {
                    totalCost += totalRows * Math.log(totalRows + 1) * 0.01 * COST_WEIGHT_CPU;
                }
            }
        }

        totalCost += totalRows * 0.001 * COST_WEIGHT_MEMORY;

        return totalCost;
    }

    private long estimateTotalResultRows(JoinQuery query, Set<String> shardIds,
                                          Map<String, Long> shardEstimatedRows) {
        if (query.getJoinConditions() == null || query.getJoinConditions().isEmpty()) {
            return shardEstimatedRows.values().stream().mapToLong(Long::longValue).sum();
        }

        long totalResultRows = 0;
        for (String shardId : shardIds) {
            long shardJoinRows = 0;
            for (JoinCondition cond : query.getJoinConditions()) {
                String leftTable = findTableByAlias(query, cond.getLeftTable()).getTableName();
                String rightTable = findTableByAlias(query, cond.getRightTable()).getTableName();

                shardJoinRows += statisticsManager.estimateJoinRows(
                        shardId,
                        leftTable, cond.getLeftColumn(),
                        rightTable, cond.getRightColumn());
            }
            totalResultRows += shardJoinRows;
        }

        return Math.max(1, totalResultRows);
    }

    private long estimateExecutionTime(QueryPlan plan, JoinQuery query, Set<String> shardIds) {
        long maxShardTime = 0;

        for (Map.Entry<String, List<String>> entry : plan.getShardQueries().entrySet()) {
            String shardId = entry.getKey();
            int queryCount = entry.getValue().size();
            long estRows = plan.getShardEstimatedRows().getOrDefault(shardId, 100L);

            long shardTime = (long) (queryCount * 5 + estRows * 0.01);
            maxShardTime = Math.max(maxShardTime, shardTime);
        }

        long joinTime = 0;
        if (query.getJoinConditions() != null) {
            long totalRows = plan.getEstimatedRows();
            if ("HASH_JOIN".equals(plan.getJoinAlgorithm())) {
                joinTime = (long) (totalRows * 0.005);
            } else {
                joinTime = (long) (totalRows * 0.01);
            }
        }

        long timeoutMs = query.getTimeoutMs() > 0 ? query.getTimeoutMs() : 30000;

        return Math.min(maxShardTime + joinTime + 100, timeoutMs);
    }

    private void buildExecutionSteps(QueryPlan plan, JoinQuery query, Set<String> shardIds) {
        int stepOrder = 0;

        if (plan.isUsePredicatePushdown()) {
            plan.addStep(QueryPlan.PlanStep.builder()
                    .stepOrder(stepOrder++)
                    .stepType("PREDICATE_PUSHDOWN")
                    .description("Push eligible predicates down to shard level")
                    .estimatedCost(plan.getEstimatedCost() * 0.01)
                    .estimatedRows(plan.getEstimatedRows())
                    .details(Collections.singletonMap("pushdownCount",
                            countPushdownPredicates(query)))
                    .build());
        }

        plan.addStep(QueryPlan.PlanStep.builder()
                .stepOrder(stepOrder++)
                .stepType("PARALLEL_SCAN")
                .description(String.format("Execute parallel sub-queries across %d shards", shardIds.size()))
                .estimatedCost(plan.getEstimatedCost() * 0.6)
                .estimatedRows(plan.getShardEstimatedRows().values().stream()
                        .mapToLong(Long::longValue).sum())
                .details(new HashMap<String, Object>() {{
                    put("shardCount", shardIds.size());
                    put("shardQueries", plan.getShardQueries());
                    put("shardEstimates", plan.getShardEstimatedRows());
                }})
                .build());

        if (query.getJoinConditions() != null && !query.getJoinConditions().isEmpty()) {
            plan.addStep(QueryPlan.PlanStep.builder()
                    .stepOrder(stepOrder++)
                    .stepType(plan.getJoinAlgorithm())
                    .description(String.format("Perform %s on %d join condition(s)",
                            plan.getJoinAlgorithm(), query.getJoinConditions().size()))
                    .estimatedCost(plan.getEstimatedCost() * 0.3)
                    .estimatedRows(plan.getEstimatedRows())
                    .details(new HashMap<String, Object>() {{
                        put("joinType", plan.getJoinAlgorithm());
                        put("conditions", query.getJoinConditions().stream()
                                .map(JoinCondition::toSql).toArray());
                    }})
                    .build());
        }

        if (query.getGroupByColumns() != null && !query.getGroupByColumns().isEmpty()) {
            plan.addStep(QueryPlan.PlanStep.builder()
                    .stepOrder(stepOrder++)
                    .stepType("AGGREGATION")
                    .description("Group by columns: " + String.join(", ", query.getGroupByColumns()))
                    .estimatedCost(plan.getEstimatedCost() * 0.05)
                    .estimatedRows(plan.getEstimatedRows() / 10)
                    .details(Collections.singletonMap("groupBy", query.getGroupByColumns()))
                    .build());
        }

        if (query.getOrderByColumns() != null && !query.getOrderByColumns().isEmpty()) {
            plan.addStep(QueryPlan.PlanStep.builder()
                    .stepOrder(stepOrder++)
                    .stepType("SORT")
                    .description("Sort by: " + String.join(", ", query.getOrderByColumns()))
                    .estimatedCost(plan.getEstimatedCost() * 0.04)
                    .estimatedRows(plan.getEstimatedRows())
                    .details(Collections.singletonMap("orderBy", query.getOrderByColumns()))
                    .build());
        }

        if (query.getLimit() != null) {
            plan.addStep(QueryPlan.PlanStep.builder()
                    .stepOrder(stepOrder)
                    .stepType("LIMIT")
                    .description(String.format("Limit %d%s rows",
                            query.getLimit(),
                            query.getOffset() != null ? " offset " + query.getOffset() : ""))
                    .estimatedCost(plan.getEstimatedCost() * 0.001)
                    .estimatedRows(Math.min(plan.getEstimatedRows(), query.getLimit()))
                    .details(new HashMap<String, Object>() {{
                        put("limit", query.getLimit());
                        if (query.getOffset() != null) put("offset", query.getOffset());
                    }})
                    .build());
        }
    }

    private int countPushdownPredicates(JoinQuery query) {
        int count = 0;
        if (query.getTables() != null) {
            for (JoinTable table : query.getTables()) {
                if (table.getPredicates() != null) {
                    for (Predicate p : table.getPredicates()) {
                        if (p.canPushdown()) count++;
                    }
                }
            }
        }
        return count;
    }

    private JoinTable findTableByAlias(JoinQuery query, String alias) {
        if (query.getTables() == null) return null;
        for (JoinTable table : query.getTables()) {
            if (alias.equals(table.getAlias()) || alias.equals(table.getTableName())) {
                return table;
            }
        }
        return null;
    }
}
