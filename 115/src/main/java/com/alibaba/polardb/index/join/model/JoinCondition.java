package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class JoinCondition {
    private String leftTable;
    private String leftColumn;
    private String rightTable;
    private String rightColumn;
    private String operator;

    public enum JoinType {
        INNER("INNER JOIN"),
        LEFT("LEFT JOIN"),
        RIGHT("RIGHT JOIN"),
        FULL("FULL JOIN"),
        CROSS("CROSS JOIN");

        private final String keyword;

        JoinType(String keyword) {
            this.keyword = keyword;
        }

        public String getKeyword() {
            return keyword;
        }
    }

    private JoinType joinType;

    public String toSql() {
        return String.format("%s.%s %s %s.%s",
                leftTable, leftColumn,
                operator != null ? operator : "=",
                rightTable, rightColumn);
    }
}
