package com.alibaba.polardb.index.join.engine;

import com.alibaba.polardb.index.join.model.JoinCondition;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.util.*;

@Component
public class NestedLoopJoinEngine {
    private static final Logger logger = LoggerFactory.getLogger(NestedLoopJoinEngine.class);

    public List<Map<String, Object>> join(
            List<Map<String, Object>> outerRows,
            List<Map<String, Object>> innerRows,
            JoinCondition condition,
            String outerAlias,
            String innerAlias) {

        if (outerRows == null || outerRows.isEmpty() || innerRows == null) {
            return Collections.emptyList();
        }

        long startTime = System.currentTimeMillis();
        String outerKey = condition.getLeftColumn();
        String innerKey = condition.getRightColumn();

        if (outerAlias != null && !outerKey.contains(".")) {
            outerKey = outerAlias + "." + outerKey;
        }
        if (innerAlias != null && !innerKey.contains(".")) {
            innerKey = innerAlias + "." + innerKey;
        }

        Map<Object, List<Map<String, Object>>> innerIndex = buildInnerIndex(
                innerRows, extractColumnName(innerKey));

        List<Map<String, Object>> result = new ArrayList<>();
        String outerKeyColumn = extractColumnName(outerKey);

        for (Map<String, Object> outerRow : outerRows) {
            Object outerValue = outerRow.get(outerKeyColumn);
            List<Map<String, Object>> matchingInnerRows = innerIndex.get(outerValue);

            if (matchingInnerRows != null && !matchingInnerRows.isEmpty()) {
                for (Map<String, Object> innerRow : matchingInnerRows) {
                    Map<String, Object> merged = mergeRows(
                            outerRow, innerRow, outerAlias, innerAlias);
                    result.add(merged);
                }
            } else if (condition.getJoinType() == JoinCondition.JoinType.LEFT
                    || condition.getJoinType() == JoinCondition.JoinType.FULL) {
                Map<String, Object> merged = mergeRows(
                        outerRow, null, outerAlias, innerAlias);
                result.add(merged);
            }
        }

        if (condition.getJoinType() == JoinCondition.JoinType.RIGHT
                || condition.getJoinType() == JoinCondition.JoinType.FULL) {
            Set<Object> matchedOuterKeys = new HashSet<>();
            for (Map<String, Object> outerRow : outerRows) {
                matchedOuterKeys.add(outerRow.get(outerKeyColumn));
            }

            for (Map<String, Object> innerRow : innerRows) {
                Object innerValue = innerRow.get(extractColumnName(innerKey));
                if (!matchedOuterKeys.contains(innerValue)) {
                    Map<String, Object> merged = mergeRows(
                            null, innerRow, outerAlias, innerAlias);
                    result.add(merged);
                }
            }
        }

        long duration = System.currentTimeMillis() - startTime;
        logger.debug("Nested loop join completed: {} rows, {} ms, outer: {}, inner: {}",
                result.size(), duration, outerRows.size(), innerRows.size());

        return result;
    }

    private Map<Object, List<Map<String, Object>>> buildInnerIndex(
            List<Map<String, Object>> innerRows, String keyColumn) {

        Map<Object, List<Map<String, Object>>> index = new HashMap<>();
        for (Map<String, Object> row : innerRows) {
            Object key = row.get(keyColumn);
            if (key != null) {
                index.computeIfAbsent(key, k -> new ArrayList<>()).add(row);
            }
        }
        return index;
    }

    private Map<String, Object> mergeRows(
            Map<String, Object> leftRow,
            Map<String, Object> rightRow,
            String leftAlias,
            String rightAlias) {

        Map<String, Object> merged = new LinkedHashMap<>();

        if (leftRow != null) {
            for (Map.Entry<String, Object> entry : leftRow.entrySet()) {
                String key = entry.getKey();
                if (!key.contains(".") && leftAlias != null) {
                    merged.put(leftAlias + "." + key, entry.getValue());
                } else {
                    merged.put(key, entry.getValue());
                }
            }
        }

        if (rightRow != null) {
            for (Map.Entry<String, Object> entry : rightRow.entrySet()) {
                String key = entry.getKey();
                if (!key.contains(".") && rightAlias != null) {
                    String prefixedKey = rightAlias + "." + key;
                    if (!merged.containsKey(prefixedKey)) {
                        merged.put(prefixedKey, entry.getValue());
                    }
                } else if (!merged.containsKey(key)) {
                    merged.put(key, entry.getValue());
                }
            }
        }

        return merged;
    }

    private String extractColumnName(String key) {
        if (key.contains(".")) {
            return key.substring(key.indexOf('.') + 1);
        }
        return key;
    }

    public long estimateCost(long outerRows, long innerRows) {
        return outerRows * (long) (Math.log(innerRows) + 1);
    }
}
