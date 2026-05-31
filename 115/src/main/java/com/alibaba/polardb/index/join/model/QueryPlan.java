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
public class QueryPlan {
    private String planId;
    private JoinQuery originalQuery;
    private JoinStrategy joinStrategy;
    private double estimatedCost;
    private double estimatedRows;
    private long estimatedTimeMs;
    private List<PlanStep> steps;
    private Map<String, List<String>> shardQueries;
    private String joinAlgorithm;
    private boolean usePredicatePushdown;
    private boolean useResultCache;
    private Map<String, Long> shardEstimatedRows;

    public enum JoinStrategy {
        BROADCAST,
        PARTITIONED,
        LOOKUP
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class PlanStep {
        private int stepOrder;
        private String stepType;
        private String description;
        private double estimatedCost;
        private long estimatedRows;
        private Map<String, Object> details;
    }

    public void addStep(PlanStep step) {
        if (steps == null) {
            steps = new ArrayList<>();
        }
        steps.add(step);
    }

    public String toExplainString() {
        StringBuilder sb = new StringBuilder();
        sb.append("=== Query Execution Plan ===\n");
        sb.append("Query ID: ").append(planId).append("\n");
        sb.append("Join Strategy: ").append(joinStrategy).append("\n");
        sb.append("Join Algorithm: ").append(joinAlgorithm).append("\n");
        sb.append("Estimated Cost: ").append(String.format("%.2f", estimatedCost)).append("\n");
        sb.append("Estimated Rows: ").append(estimatedRows).append("\n");
        sb.append("Estimated Time: ").append(estimatedTimeMs).append(" ms\n");
        sb.append("Predicate Pushdown: ").append(usePredicatePushdown ? "ENABLED" : "DISABLED").append("\n");
        sb.append("Result Cache: ").append(useResultCache ? "ENABLED" : "DISABLED").append("\n");
        sb.append("\n=== Execution Steps ===\n");

        if (steps != null) {
            for (PlanStep step : steps) {
                sb.append(String.format("Step %d: [%s] %s\n",
                        step.getStepOrder(), step.getStepType(), step.getDescription()));
                sb.append(String.format("  Cost: %.2f, Rows: %d\n",
                        step.getEstimatedCost(), step.getEstimatedRows()));
                if (step.getDetails() != null && !step.getDetails().isEmpty()) {
                    sb.append("  Details: ").append(step.getDetails()).append("\n");
                }
            }
        }

        if (shardQueries != null && !shardQueries.isEmpty()) {
            sb.append("\n=== Shard Queries ===\n");
            for (Map.Entry<String, List<String>> entry : shardQueries.entrySet()) {
                sb.append("Shard ").append(entry.getKey()).append(":\n");
                for (String sql : entry.getValue()) {
                    sb.append("  -> ").append(sql).append("\n");
                }
                if (shardEstimatedRows != null && shardEstimatedRows.containsKey(entry.getKey())) {
                    sb.append("     Estimated rows: ").append(shardEstimatedRows.get(entry.getKey())).append("\n");
                }
            }
        }

        return sb.toString();
    }
}
