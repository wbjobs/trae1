package com.dbagent.indexadvisor;

import com.dbagent.indexadvisor.StatisticsCollector.ColumnStatistics;
import com.dbagent.indexadvisor.StatisticsCollector.TableStatistics;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class IndexAdvisor {

    private static final double HIGH_SELECTIVITY_THRESHOLD = 0.1;
    private static final double MEDIUM_SELECTIVITY_THRESHOLD = 0.5;
    private static final int MIN_ROW_COUNT_FOR_INDEX = 1000;
    private static final int MAX_INDEX_COLUMNS = 5;

    private final StatisticsCollector statisticsCollector;

    public IndexAdvisor() {
        this.statisticsCollector = StatisticsCollector.getInstance();
    }

    public IndexAdvisor(StatisticsCollector statisticsCollector) {
        this.statisticsCollector = statisticsCollector;
    }

    public List<IndexSuggestion> analyze(String sql, String databaseType,
                                         java.sql.Connection connection,
                                         long executionTimeMs) {
        List<IndexSuggestion> suggestions = new ArrayList<>();

        if (sql == null || sql.trim().isEmpty()) {
            return suggestions;
        }

        SqlParser.ParsedSql parsed = SqlParser.parse(sql);

        if (!isDmlOperation(parsed.getOperation())) {
            return suggestions;
        }

        String mainTable = parsed.getMainTable();
        if (mainTable == null) {
            return suggestions;
        }

        TableStatistics tableStats = statisticsCollector.collect(connection, mainTable);

        List<String> whereColumns = parsed.getWhereColumns();
        List<String> joinColumns = parsed.getJoinColumns();
        List<String> orderByColumns = parsed.getOrderByColumns();
        List<String> groupByColumns = parsed.getGroupByColumns();

        List<String> candidateColumns = new ArrayList<>();
        for (String col : whereColumns) {
            if (!candidateColumns.contains(col)) {
                candidateColumns.add(col);
            }
        }
        for (String col : joinColumns) {
            if (!candidateColumns.contains(col)) {
                candidateColumns.add(col);
            }
        }

        if (candidateColumns.isEmpty()) {
            return suggestions;
        }

        if (tableStats != null && tableStats.getRowCount() > 0) {
            if (tableStats.getRowCount() < MIN_ROW_COUNT_FOR_INDEX) {
                return suggestions;
            }
        }

        List<String> indexedColumns = new ArrayList<>();
        List<String> unindexedColumns = new ArrayList<>();

        for (String col : candidateColumns) {
            if (tableStats != null && tableStats.hasIndexOn(col)) {
                indexedColumns.add(col);
            } else {
                unindexedColumns.add(col);
            }
        }

        if (!unindexedColumns.isEmpty()) {
            List<String> recommendedColumns = selectBestColumns(
                    unindexedColumns, tableStats, executionTimeMs);

            if (!recommendedColumns.isEmpty()) {
                IndexSuggestion suggestion = createSuggestion(
                        mainTable, recommendedColumns, sql,
                        databaseType, executionTimeMs, tableStats);
                suggestion.setOriginalSql(sql);
                suggestions.add(suggestion);
            }
        }

        if (suggestions.isEmpty() && !orderByColumns.isEmpty()) {
            List<String> unindexedOrderCols = new ArrayList<>();
            for (String col : orderByColumns) {
                if (tableStats == null || !tableStats.hasIndexOn(col)) {
                    unindexedOrderCols.add(col);
                }
            }

            for (String col : unindexedOrderCols) {
                if (!candidateColumns.contains(col) &&
                        (tableStats == null || !tableStats.hasIndexOn(col))) {
                    List<String> orderCandidate = new ArrayList<>();
                    orderCandidate.add(col);
                    if (!whereColumns.isEmpty() && !tableStats.hasIndexOn(whereColumns.get(0))) {
                        orderCandidate.add(0, whereColumns.get(0));
                    }
                    IndexSuggestion suggestion = createSuggestion(
                            mainTable, orderCandidate, sql,
                            databaseType, executionTimeMs, tableStats);
                    suggestion.setOriginalSql(sql);
                    suggestion.setReason("ORDER BY optimization");
                    suggestions.add(suggestion);
                }
            }
        }

        if (suggestions.isEmpty() && !groupByColumns.isEmpty()) {
            List<String> unindexedGroupCols = new ArrayList<>();
            for (String col : groupByColumns) {
                if (tableStats == null || !tableStats.hasIndexOn(col)) {
                    if (!candidateColumns.contains(col)) {
                        unindexedGroupCols.add(col);
                    }
                }
            }

            if (!unindexedGroupCols.isEmpty()) {
                List<String> groupCandidate = new ArrayList<>();
                for (String col : whereColumns) {
                    if (!candidateColumns.contains(col) || !tableStats.hasIndexOn(col)) {
                        if (!groupCandidate.contains(col) && !tableStats.hasIndexOn(col)) {
                            groupCandidate.add(col);
                        }
                    }
                }
                groupCandidate.addAll(unindexedGroupCols);

                if (!groupCandidate.isEmpty()) {
                    IndexSuggestion suggestion = createSuggestion(
                            mainTable, groupCandidate, sql,
                            databaseType, executionTimeMs, tableStats);
                    suggestion.setOriginalSql(sql);
                    suggestion.setReason("GROUP BY optimization");
                    suggestions.add(suggestion);
                }
            }
        }

        return suggestions;
    }

    private boolean isDmlOperation(String operation) {
        return "SELECT".equals(operation) ||
                "UPDATE".equals(operation) ||
                "DELETE".equals(operation) ||
                "WITH".equals(operation);
    }

    private List<String> selectBestColumns(List<String> columns,
                                           TableStatistics tableStats,
                                           long executionTimeMs) {
        List<String> selected = new ArrayList<>();
        Set<String> selectedSet = new HashSet<>();

        if (columns.isEmpty()) {
            return selected;
        }

        for (String col : columns) {
            if (selected.size() >= MAX_INDEX_COLUMNS) {
                break;
            }

            if (selectedSet.contains(col)) {
                continue;
            }

            if (tableStats != null) {
                double selectivity = estimateSelectivity(col, tableStats);
                if (selectivity > HIGH_SELECTIVITY_THRESHOLD || executionTimeMs > 500) {
                    selected.add(col);
                    selectedSet.add(col);
                } else if (selectivity > 0 && executionTimeMs > 200) {
                    selected.add(col);
                    selectedSet.add(col);
                }
            } else {
                if (executionTimeMs > 200) {
                    selected.add(col);
                    selectedSet.add(col);
                }
            }
        }

        if (selected.isEmpty() && executionTimeMs > 200) {
            for (String col : columns) {
                if (selected.size() >= MAX_INDEX_COLUMNS) {
                    break;
                }
                if (!selectedSet.contains(col)) {
                    selected.add(col);
                    selectedSet.add(col);
                }
            }
        }

        return selected;
    }

    private double estimateSelectivity(String column, TableStatistics tableStats) {
        if (tableStats == null || tableStats.getRowCount() == 0) {
            return 0.5;
        }

        long rowCount = tableStats.getRowCount();

        if (tableStats.getUniqueColumns().contains(column)) {
            return 1.0 / rowCount;
        }

        for (ColumnStatistics colStats : tableStats.getColumns()) {
            if (colStats.getColumnName().equalsIgnoreCase(column)) {
                if (colStats.getDistinctValues() > 0) {
                    return (double) colStats.getDistinctValues() / rowCount;
                }
                if ("varchar".equalsIgnoreCase(colStats.getDataType()) ||
                        "text".equalsIgnoreCase(colStats.getDataType()) ||
                        "character varying".equalsIgnoreCase(colStats.getDataType())) {
                    return 0.3;
                }
                if ("int".equalsIgnoreCase(colStats.getDataType()) ||
                        "integer".equalsIgnoreCase(colStats.getDataType()) ||
                        "bigint".equalsIgnoreCase(colStats.getDataType())) {
                    return 0.4;
                }
                if ("timestamp".equalsIgnoreCase(colStats.getDataType()) ||
                        "date".equalsIgnoreCase(colStats.getDataType()) ||
                        "datetime".equalsIgnoreCase(colStats.getDataType())) {
                    return 0.6;
                }
                if ("boolean".equalsIgnoreCase(colStats.getDataType())) {
                    return 0.02;
                }
                break;
            }
        }

        return 0.5;
    }

    private IndexSuggestion createSuggestion(String tableName, List<String> columns,
                                             String originalSql, String databaseType,
                                             long executionTimeMs,
                                             TableStatistics tableStats) {
        IndexSuggestion suggestion = new IndexSuggestion();
        suggestion.setTableName(tableName);
        suggestion.setColumns(columns);
        suggestion.setDatabaseType(databaseType);
        suggestion.setExecutionTimeMs(executionTimeMs);

        if (tableStats != null) {
            suggestion.setEstimatedCardinality(tableStats.getRowCount());
            if (!columns.isEmpty()) {
                suggestion.setEstimatedSelectivity(
                        estimateSelectivity(columns.get(0), tableStats));
            }
        }

        if (executionTimeMs > 1000) {
            suggestion.setPriority(IndexSuggestion.Priority.HIGH);
        } else if (executionTimeMs > 500) {
            suggestion.setPriority(IndexSuggestion.Priority.MEDIUM);
        } else {
            suggestion.setPriority(IndexSuggestion.Priority.LOW);
        }

        StringBuilder reason = new StringBuilder();
        reason.append("Slow SQL detected (").append(executionTimeMs).append("ms)");
        if (tableStats != null && tableStats.getRowCount() > 0) {
            reason.append(", table has ").append(tableStats.getRowCount()).append(" rows");
        }
        if (!columns.isEmpty()) {
            reason.append(", columns: ").append(String.join(", ", columns));
        }
        suggestion.setReason(reason.toString());

        return suggestion;
    }
}
