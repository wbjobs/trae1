package com.cdc.common.util;

import com.cdc.common.config.TableFilterConfig;
import org.apache.kafka.common.utils.Utils;

import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class TableNameUtil {

    private static final Map<String, TableFilterConfig> tableFilterCache = new ConcurrentHashMap<>();
    private static String topicPrefix = "";

    public static void setTopicPrefix(String prefix) {
        topicPrefix = prefix == null ? "" : prefix;
    }

    public static void registerTableFilters(List<TableFilterConfig> filters) {
        tableFilterCache.clear();
        if (filters != null) {
            filters.forEach(filter -> tableFilterCache.put(filter.getTableName(), filter));
        }
    }

    public static TableFilterConfig getTableFilter(String fullTableName) {
        return tableFilterCache.get(fullTableName);
    }

    public static String getTopicName(String fullTableName) {
        TableFilterConfig filter = tableFilterCache.get(fullTableName);
        if (filter != null && filter.getTopicName() != null && !filter.getTopicName().isEmpty()) {
            return filter.getTopicName();
        }
        return buildTopicName(fullTableName);
    }

    private static String buildTopicName(String fullTableName) {
        String sanitized = fullTableName.replaceAll("[^a-zA-Z0-9._-]", "_");
        return topicPrefix.isEmpty() ? sanitized : topicPrefix + "." + sanitized;
    }

    public static String parseFullTableName(String schemaName, String tableName) {
        if (schemaName != null && !schemaName.isEmpty()) {
            return schemaName + "." + tableName;
        }
        return tableName;
    }

    public static int getPartition(String key, int numPartitions) {
        if (key == null) {
            return 0;
        }
        return Utils.toPositive(Utils.murmur2(key.getBytes())) % numPartitions;
    }
}
