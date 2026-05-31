package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ShardStatistics {
    private String shardId;
    private long totalRows;
    private long lastAnalyzeTime;
    private Map<String, Long> tableRowCounts;
    private Map<String, Map<String, Long>> columnCardinalities;
    private double avgQueryLatencyMs;
    private long queryCount;

    public ShardStatistics(String shardId) {
        this.shardId = shardId;
        this.tableRowCounts = new ConcurrentHashMap<>();
        this.columnCardinalities = new ConcurrentHashMap<>();
    }

    public Long getTableRowCount(String tableName) {
        return tableRowCounts.getOrDefault(tableName, 1000L);
    }

    public Long getColumnCardinality(String tableName, String columnName) {
        Map<String, Long> colMap = columnCardinalities.get(tableName);
        if (colMap != null) {
            return colMap.getOrDefault(columnName, getTableRowCount(tableName) / 10);
        }
        return getTableRowCount(tableName) / 10;
    }

    public double getSelectivity(String tableName, String columnName, String operator, Object value) {
        long cardinality = getColumnCardinality(tableName, columnName);
        long rows = getTableRowCount(tableName);

        if (rows <= 0) return 1.0;

        switch (operator) {
            case "=":
            case "EQ":
                return Math.min(1.0, 1.0 / Math.max(1, cardinality));
            case ">":
            case ">=":
            case "<":
            case "<=":
                return 0.33;
            case "LIKE":
                return 0.1;
            case "IN":
                if (value instanceof Object[]) {
                    return Math.min(1.0, ((Object[]) value).length * 1.0 / Math.max(1, cardinality));
                }
                return 0.1;
            default:
                return 1.0;
        }
    }
}
