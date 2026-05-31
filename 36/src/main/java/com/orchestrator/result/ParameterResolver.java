package com.orchestrator.result;

import com.orchestrator.zookeeper.ZkService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class ParameterResolver {

    private static final Logger logger = LoggerFactory.getLogger(ParameterResolver.class);

    private static final Pattern PLACEHOLDER_PATTERN = Pattern.compile("\\{\\{\\s*([\\w.]+)\\s*\\}\\}");

    private final ResultStore resultStore;
    private final String dagId;

    public ParameterResolver(ResultStore resultStore, String dagId) {
        this.resultStore = resultStore;
        this.dagId = dagId;
    }

    public Map<String, Object> resolveParams(Map<String, Object> params) {
        if (params == null || params.isEmpty()) {
            return params;
        }
        Map<String, Object> resolved = new HashMap<>();
        for (Map.Entry<String, Object> entry : params.entrySet()) {
            resolved.put(entry.getKey(), resolveValue(entry.getValue()));
        }
        return resolved;
    }

    @SuppressWarnings("unchecked")
    public Object resolveValue(Object value) {
        if (value == null) return null;

        if (value instanceof String) {
            return resolveString((String) value);
        }

        if (value instanceof Map) {
            Map<String, Object> resolvedMap = new HashMap<>();
            for (Map.Entry<String, Object> entry : ((Map<String, Object>) value).entrySet()) {
                resolvedMap.put(entry.getKey(), resolveValue(entry.getValue()));
            }
            return resolvedMap;
        }

        if (value instanceof List) {
            List<Object> resolvedList = new ArrayList<>();
            for (Object item : (List<Object>) value) {
                resolvedList.add(resolveValue(item));
            }
            return resolvedList;
        }

        return value;
    }

    private Object resolveString(String value) {
        Matcher matcher = PLACEHOLDER_PATTERN.matcher(value);
        if (!matcher.find()) {
            return value;
        }

        matcher.reset();
        StringBuffer sb = new StringBuffer();
        while (matcher.find()) {
            String fullReference = matcher.group(1);
            Object resolved = resolveReference(fullReference);
            if (resolved != null) {
                matcher.appendReplacement(sb, Matcher.quoteReplacement(resolved.toString()));
            } else {
                logger.warn("Could not resolve placeholder: {{}}", fullReference);
                matcher.appendReplacement(sb, Matcher.quoteReplacement("null"));
            }
        }
        matcher.appendTail(sb);
        return sb.toString();
    }

    private Object resolveReference(String reference) {
        String[] parts = reference.split("\\.", 2);
        if (parts.length == 0) return null;

        String taskId = parts[0];
        String fieldPath = parts.length > 1 ? parts[1] : null;

        if (fieldPath == null || fieldPath.isEmpty()) {
            return resultStore.getResultRaw(dagId, taskId);
        }

        if (fieldPath.startsWith("output.")) {
            fieldPath = fieldPath.substring("output.".length());
        } else if (fieldPath.equals("output")) {
            return resultStore.getResultRaw(dagId, taskId);
        }

        Object value = resultStore.getField(dagId, taskId, fieldPath);
        if (value != null) {
            return value;
        }

        return resultStore.getField(dagId, taskId, reference);
    }

    public boolean hasUnresolvedPlaceholders(Object value) {
        if (value == null) return false;
        if (value instanceof String) {
            return PLACEHOLDER_PATTERN.matcher((String) value).find();
        }
        if (value instanceof Map) {
            for (Object v : ((Map<?, ?>) value).values()) {
                if (hasUnresolvedPlaceholders(v)) return true;
            }
        }
        if (value instanceof List) {
            for (Object v : (List<?>) value) {
                if (hasUnresolvedPlaceholders(v)) return true;
            }
        }
        return false;
    }

    public Set<String> extractUpstreamTaskIds(Map<String, Object> params) {
        Set<String> taskIds = new HashSet<>();
        if (params == null) return taskIds;
        extractFromValue(params, taskIds);
        return taskIds;
    }

    @SuppressWarnings("unchecked")
    private void extractFromValue(Object value, Set<String> taskIds) {
        if (value == null) return;
        if (value instanceof String) {
            Matcher matcher = PLACEHOLDER_PATTERN.matcher((String) value);
            while (matcher.find()) {
                String ref = matcher.group(1);
                String taskId = ref.split("\\.")[0];
                taskIds.add(taskId);
            }
        } else if (value instanceof Map) {
            for (Object v : ((Map<String, Object>) value).values()) {
                extractFromValue(v, taskIds);
            }
        } else if (value instanceof List) {
            for (Object v : (List<Object>) value) {
                extractFromValue(v, taskIds);
            }
        }
    }
}
