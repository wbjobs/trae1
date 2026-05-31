package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class JoinResult {
    private String queryId;
    private boolean success;
    private String errorMessage;
    private List<Map<String, Object>> rows;
    private int totalRows;
    private long executionTimeMs;
    private QueryPlan executionPlan;
    private boolean fromCache;
    private Map<String, Long> shardQueryTimes;
    private long joinTimeMs;
    private int shardCount;

    public JoinResult addRow(Map<String, Object> row) {
        if (rows == null) {
            rows = new ArrayList<>();
        }
        rows.add(row);
        totalRows = rows.size();
        return this;
    }
}
