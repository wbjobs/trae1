package com.alibaba.polardb.index.join.engine;

import com.alibaba.polardb.index.join.model.JoinCondition;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.util.*;

@Component
public class HashJoinEngine {
    private static final Logger logger = LoggerFactory.getLogger(HashJoinEngine.class);

    public List<Map<String, Object>> join(
            List<Map<String, Object>> leftRows,
            List<Map<String, Object>> rightRows,
            JoinCondition condition,
            String leftAlias,
            String rightAlias) {

        if (leftRows == null || leftRows.isEmpty() || rightRows == null || rightRows.isEmpty()) {
            return Collections.emptyList();
        }

        long startTime = System.currentTimeMillis();
        String leftKey = condition.getLeftColumn();
        String rightKey = condition.getRightColumn();

        if (leftAlias != null && !leftKey.contains(".")) {
            leftKey = leftAlias + "." + leftKey;
        }
        if (rightAlias != null && !rightKey.contains(".")) {
            rightKey = rightAlias + "." + rightKey;
        }

        List<Map<String, Object>> result;

        if (leftRows.size() <= rightRows.size()) {
            result = buildAndProbe(
                    leftRows, rightRows,
                    leftKey, rightKey,
                    leftAlias, rightAlias,
                    condition.getJoinType(),
                    true);
        } else {
            result = buildAndProbe(
                    rightRows, leftRows,
                    rightKey, leftKey,
                    rightAlias, leftAlias,
                    swapJoinType(condition.getJoinType()),
                    false);
        }

        long duration = System.currentTimeMillis() - startTime;
        logger.debug("Hash join completed: {} rows, {} ms, join type: {}",
                result.size(), duration, condition.getJoinType());

        return result;
    }

    private List<Map<String, Object>> buildAndProbe(
            List<Map<String, Object>> buildRows,
            List<Map<String, Object>> probeRows,
            String buildKey,
            String probeKey,
            String buildAlias,
            String probeAlias,
            JoinCondition.JoinType joinType,
            boolean isLeftBuild) {

        Map<Object, List<Map<String, Object>>> hashTable = new HashMap<>();

        String buildKeyColumn = extractColumnName(buildKey);
        for (Map<String, Object> row : buildRows) {
            Object key = row.get(buildKeyColumn);
            if (key == null) {
                continue;
            }
            hashTable.computeIfAbsent(key, k -> new ArrayList<>()).add(row);
        }

        logger.debug("Hash table built: {} keys, {} total rows",
                hashTable.size(), buildRows.size());

        List<Map<String, Object>> result = new ArrayList<>();
        String probeKeyColumn = extractColumnName(probeKey);
        Set<Integer> matchedBuildIndices = new HashSet<>();

        for (Map<String, Object> probeRow : probeRows) {
            Object key = probeRow.get(probeKeyColumn);
            List<Map<String, Object>> matchingBuildRows = hashTable.get(key);

            if (matchingBuildRows != null) {
                for (Map<String, Object> buildRow : matchingBuildRows) {
                    Map<String, Object> mergedRow;
                    if (isLeftBuild) {
                        mergedRow = mergeRows(buildRow, probeRow, buildAlias, probeAlias);
                    } else {
                        mergedRow = mergeRows(probeRow, buildRow, probeAlias, buildAlias);
                    }
                    result.add(mergedRow);
                }
            } else if (joinType == JoinCondition.JoinType.LEFT
                    || joinType == JoinCondition.JoinType.FULL) {
                Map<String, Object> mergedRow;
                if (isLeftBuild) {
                    mergedRow = mergeRows(null, probeRow, buildAlias, probeAlias);
                } else {
                    mergedRow = mergeRows(probeRow, null, probeAlias, buildAlias);
                }
                result.add(mergedRow);
            }
        }

        if (joinType == JoinCondition.JoinType.RIGHT
                || joinType == JoinCondition.JoinType.FULL) {
            for (Map<String, Object> buildRow : buildRows) {
                Object key = buildRow.get(buildKeyColumn);
                if (!matchedBuildIndices.contains(System.identityHashCode(buildRow))) {
                    boolean found = false;
                    for (Map<String, Object> probeRow : probeRows) {
                        Object probeKeyValue = probeRow.get(probeKeyColumn);
                        if (key != null && key.equals(probeKeyValue)) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        Map<String, Object> mergedRow;
                        if (isLeftBuild) {
                            mergedRow = mergeRows(buildRow, null, buildAlias, probeAlias);
                        } else {
                            mergedRow = mergeRows(null, buildRow, probeAlias, buildAlias);
                        }
                        result.add(mergedRow);
                    }
                }
            }
        }

        return result;
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

    private JoinCondition.JoinType swapJoinType(JoinCondition.JoinType type) {
        if (type == JoinCondition.JoinType.LEFT) {
            return JoinCondition.JoinType.RIGHT;
        } else if (type == JoinCondition.JoinType.RIGHT) {
            return JoinCondition.JoinType.LEFT;
        }
        return type;
    }
}
