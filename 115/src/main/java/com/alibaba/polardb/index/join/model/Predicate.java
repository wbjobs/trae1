package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class Predicate {
    private String column;
    private String operator;
    private Object value;
    private String tableAlias;
    private boolean pushdownSupported;

    public enum Operator {
        EQ("="),
        NE("!="),
        GT(">"),
        GTE(">="),
        LT("<"),
        LTE("<="),
        LIKE("LIKE"),
        IN("IN"),
        BETWEEN("BETWEEN"),
        IS_NULL("IS NULL"),
        IS_NOT_NULL("IS NOT NULL");

        private final String symbol;

        Operator(String symbol) {
            this.symbol = symbol;
        }

        public String getSymbol() {
            return symbol;
        }

        public static Operator fromSymbol(String symbol) {
            for (Operator op : values()) {
                if (op.symbol.equalsIgnoreCase(symbol)) {
                    return op;
                }
            }
            throw new IllegalArgumentException("Unknown operator: " + symbol);
        }
    }

    public String toSql() {
        StringBuilder sb = new StringBuilder();
        if (tableAlias != null) {
            sb.append(tableAlias).append(".");
        }
        sb.append(column).append(" ");

        Operator op = Operator.valueOf(operator);
        switch (op) {
            case IS_NULL:
            case IS_NOT_NULL:
                sb.append(op.getSymbol());
                break;
            case IN:
                sb.append("IN (").append(value).append(")");
                break;
            case BETWEEN:
                Object[] range = (Object[]) value;
                sb.append("BETWEEN '").append(range[0]).append("' AND '").append(range[1]).append("'");
                break;
            default:
                if (value instanceof String) {
                    sb.append(op.getSymbol()).append(" '").append(value).append("'");
                } else {
                    sb.append(op.getSymbol()).append(" ").append(value);
                }
        }
        return sb.toString();
    }

    public boolean canPushdown() {
        return pushdownSupported;
    }
}
