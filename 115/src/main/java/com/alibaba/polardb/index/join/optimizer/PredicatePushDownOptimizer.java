package com.alibaba.polardb.index.join.optimizer;

import com.alibaba.polardb.index.join.model.JoinQuery;
import com.alibaba.polardb.index.join.model.JoinTable;
import com.alibaba.polardb.index.join.model.Predicate;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

@Component
public class PredicatePushDownOptimizer {
    private static final Logger logger = LoggerFactory.getLogger(PredicatePushDownOptimizer.class);

    private static final Set<String> PUSHDOWN_UNSUPPORTED_FUNCTIONS = new HashSet<>(Arrays.asList(
            "NOW()", "CURRENT_DATE()", "CURRENT_TIME()", "SYSDATE()",
            "RAND()", "UUID()", "CONNECTION_ID()", "LAST_INSERT_ID()"
    ));

    private static final Pattern FUNCTION_PATTERN = Pattern.compile("\\b([A-Z_]+)\\s*\\(", Pattern.CASE_INSENSITIVE);

    public JoinQuery optimize(JoinQuery query) {
        if (query.getTables() == null) {
            return query;
        }

        for (JoinTable table : query.getTables()) {
            if (table.getPredicates() == null || table.getPredicates().isEmpty()) {
                continue;
            }

            List<Predicate> pushdownPredicates = new ArrayList<>();
            List<Predicate> remainingPredicates = new ArrayList<>();

            for (Predicate predicate : table.getPredicates()) {
                if (canPushdown(predicate)) {
                    predicate.setPushdownSupported(true);
                    pushdownPredicates.add(predicate);
                    logger.debug("Predicate can be pushed down to {}: {}",
                            table.getTableName(), predicate.toSql());
                } else {
                    predicate.setPushdownSupported(false);
                    remainingPredicates.add(predicate);
                    logger.debug("Predicate cannot be pushed down: {}", predicate.toSql());
                }
            }

            table.setPredicates(pushdownPredicates);
            if (!remainingPredicates.isEmpty()) {
                attachRemainingPredicates(query, table, remainingPredicates);
            }
        }

        return query;
    }

    private boolean canPushdown(Predicate predicate) {
        if (predicate.getValue() == null) {
            String op = predicate.getOperator();
            return "IS_NULL".equals(op) || "IS_NOT_NULL".equals(op);
        }

        if (predicate.getValue() instanceof String) {
            String valueStr = (String) predicate.getValue();
            Matcher matcher = FUNCTION_PATTERN.matcher(valueStr);
            while (matcher.find()) {
                String func = matcher.group(1).toUpperCase() + "()";
                if (PUSHDOWN_UNSUPPORTED_FUNCTIONS.contains(func)) {
                    return false;
                }
            }
        }

        if (predicate.getValue() instanceof Object[]) {
            Object[] values = (Object[]) predicate.getValue();
            for (Object v : values) {
                if (v instanceof String) {
                    Matcher matcher = FUNCTION_PATTERN.matcher((String) v);
                    while (matcher.find()) {
                        String func = matcher.group(1).toUpperCase() + "()";
                        if (PUSHDOWN_UNSUPPORTED_FUNCTIONS.contains(func)) {
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    private void attachRemainingPredicates(JoinQuery query, JoinTable table,
                                           List<Predicate> remainingPredicates) {
        if (query.getTables() == null) return;

        for (JoinTable t : query.getTables()) {
            if (t.getAlias() != null && t.getAlias().equals(table.getAlias())) {
                List<Predicate> existing = t.getPredicates();
                if (existing == null) {
                    existing = new ArrayList<>();
                    t.setPredicates(existing);
                }
                existing.addAll(remainingPredicates);
                break;
            }
        }
    }

    public String buildShardWhereClause(JoinTable table) {
        if (table.getPredicates() == null || table.getPredicates().isEmpty()) {
            return "";
        }

        List<String> clauses = new ArrayList<>();
        for (Predicate predicate : table.getPredicates()) {
            if (predicate.canPushdown()) {
                clauses.add(predicate.toSql());
            }
        }

        if (clauses.isEmpty()) {
            return "";
        }

        return " WHERE " + String.join(" AND ", clauses);
    }

    public Map<String, Object> buildShardQueryContext(JoinTable table) {
        Map<String, Object> context = new HashMap<>();
        if (table.getPredicates() == null) {
            return context;
        }

        List<Predicate> pushdown = new ArrayList<>();
        List<Predicate> local = new ArrayList<>();
        for (Predicate p : table.getPredicates()) {
            if (p.canPushdown()) {
                pushdown.add(p);
            } else {
                local.add(p);
            }
        }

        context.put("pushdownPredicates", pushdown);
        context.put("localPredicates", local);
        context.put("pushdownCount", pushdown.size());
        context.put("localFilterCount", local.size());

        return context;
    }

    public boolean applyLocalFilter(Map<String, Object> row, List<Predicate> predicates) {
        if (predicates == null || predicates.isEmpty()) {
            return true;
        }

        for (Predicate predicate : predicates) {
            if (!matchPredicate(row, predicate)) {
                return false;
            }
        }
        return true;
    }

    @SuppressWarnings("unchecked")
    private boolean matchPredicate(Map<String, Object> row, Predicate predicate) {
        Object rowValue = row.get(predicate.getColumn());
        if (rowValue == null) {
            return predicate.getOperator().equals("IS_NULL");
        }

        try {
            Predicate.Operator op = Predicate.Operator.valueOf(predicate.getOperator());
            Comparable<Object> comparable = (Comparable<Object>) rowValue;

            switch (op) {
                case EQ:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) == 0;
                case NE:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) != 0;
                case GT:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) > 0;
                case GTE:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) >= 0;
                case LT:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) < 0;
                case LTE:
                    return comparable.compareTo(convertType(rowValue, predicate.getValue())) <= 0;
                case LIKE:
                    String pattern = (String) predicate.getValue();
                    pattern = pattern.replace("%", ".*").replace("_", ".");
                    return String.valueOf(rowValue).matches(pattern);
                case IN:
                    Object[] inValues = (Object[]) predicate.getValue();
                    for (Object v : inValues) {
                        if (comparable.compareTo(convertType(rowValue, v)) == 0) {
                            return true;
                        }
                    }
                    return false;
                case BETWEEN:
                    Object[] range = (Object[]) predicate.getValue();
                    return comparable.compareTo(convertType(rowValue, range[0])) >= 0
                            && comparable.compareTo(convertType(rowValue, range[1])) <= 0;
                default:
                    return true;
            }
        } catch (Exception e) {
            logger.warn("Failed to apply local filter for column {}: {}",
                    predicate.getColumn(), e.getMessage());
            return true;
        }
    }

    @SuppressWarnings("unchecked")
    private Object convertType(Object target, Object value) {
        if (target instanceof Integer && value instanceof String) {
            return Integer.parseInt((String) value);
        } else if (target instanceof Long && value instanceof String) {
            return Long.parseLong((String) value);
        } else if (target instanceof Double && value instanceof String) {
            return Double.parseDouble((String) value);
        }
        return value;
    }
}
