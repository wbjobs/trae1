package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.ArrayList;
import java.util.List;

public class TableFilterConfig {

    @JsonProperty("tableName")
    private String tableName;

    @JsonProperty("includedColumns")
    private List<String> includedColumns = new ArrayList<>();

    @JsonProperty("excludedColumns")
    private List<String> excludedColumns = new ArrayList<>();

    @JsonProperty("topicName")
    private String topicName;

    public String getTableName() {
        return tableName;
    }

    public void setTableName(String tableName) {
        this.tableName = tableName;
    }

    public List<String> getIncludedColumns() {
        return includedColumns;
    }

    public void setIncludedColumns(List<String> includedColumns) {
        this.includedColumns = includedColumns;
    }

    public List<String> getExcludedColumns() {
        return excludedColumns;
    }

    public void setExcludedColumns(List<String> excludedColumns) {
        this.excludedColumns = excludedColumns;
    }

    public String getTopicName() {
        return topicName;
    }

    public void setTopicName(String topicName) {
        this.topicName = topicName;
    }

    public boolean isColumnIncluded(String columnName) {
        if (includedColumns != null && !includedColumns.isEmpty()) {
            return includedColumns.contains(columnName);
        }
        if (excludedColumns != null && !excludedColumns.isEmpty()) {
            return !excludedColumns.contains(columnName);
        }
        return true;
    }
}
