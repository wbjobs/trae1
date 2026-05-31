package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.ArrayList;
import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class JoinQuery {
    private String queryId;
    private List<JoinTable> tables;
    private List<JoinCondition> joinConditions;
    private List<String> groupByColumns;
    private List<String> orderByColumns;
    private Integer limit;
    private Integer offset;
    private boolean explain;
    private long timeoutMs;

    public JoinQuery addTable(JoinTable table) {
        if (tables == null) {
            tables = new ArrayList<>();
        }
        tables.add(table);
        return this;
    }

    public JoinQuery addJoinCondition(JoinCondition condition) {
        if (joinConditions == null) {
            joinConditions = new ArrayList<>();
        }
        joinConditions.add(condition);
        return this;
    }

    public String toCacheKey() {
        StringBuilder sb = new StringBuilder();
        if (tables != null) {
            for (JoinTable table : tables) {
                sb.append(table.getFullTableName()).append("|");
                if (table.getSelectColumns() != null) {
                    sb.append(String.join(",", table.getSelectColumns()));
                }
                sb.append("|");
                if (table.getPredicates() != null) {
                    for (Predicate p : table.getPredicates()) {
                        sb.append(p.toSql()).append(";");
                    }
                }
                sb.append("||");
            }
        }
        if (joinConditions != null) {
            for (JoinCondition cond : joinConditions) {
                sb.append(cond.toSql()).append("|");
            }
        }
        return sb.toString();
    }
}
