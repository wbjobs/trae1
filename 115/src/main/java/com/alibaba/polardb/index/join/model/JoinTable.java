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
public class JoinTable {
    private String tableName;
    private String alias;
    private String schema;
    private String globalIdColumn;
    private String shardKeyColumn;
    private List<String> selectColumns;
    private List<Predicate> predicates;

    public String getFullTableName() {
        return schema != null ? schema + "." + tableName : tableName;
    }

    public JoinTable addPredicate(Predicate predicate) {
        if (predicates == null) {
            predicates = new ArrayList<>();
        }
        predicates.add(predicate);
        return this;
    }

    public String getSelectClause() {
        if (selectColumns == null || selectColumns.isEmpty()) {
            return "*";
        }
        return String.join(", ", selectColumns);
    }
}
