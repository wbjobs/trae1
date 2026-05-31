package com.cdc.common.event;

import java.time.Instant;
import java.util.Map;

public class CdcEvent {

    private String sourceDatabase;
    private String schemaName;
    private String tableName;
    private OperationType operation;
    private Map<String, Object> before;
    private Map<String, Object> after;
    private Map<String, Object> source;
    private Instant timestamp;
    private String transactionId;

    public String getSourceDatabase() {
        return sourceDatabase;
    }

    public void setSourceDatabase(String sourceDatabase) {
        this.sourceDatabase = sourceDatabase;
    }

    public String getSchemaName() {
        return schemaName;
    }

    public void setSchemaName(String schemaName) {
        this.schemaName = schemaName;
    }

    public String getTableName() {
        return tableName;
    }

    public void setTableName(String tableName) {
        this.tableName = tableName;
    }

    public OperationType getOperation() {
        return operation;
    }

    public void setOperation(OperationType operation) {
        this.operation = operation;
    }

    public Map<String, Object> getBefore() {
        return before;
    }

    public void setBefore(Map<String, Object> before) {
        this.before = before;
    }

    public Map<String, Object> getAfter() {
        return after;
    }

    public void setAfter(Map<String, Object> after) {
        this.after = after;
    }

    public Map<String, Object> getSource() {
        return source;
    }

    public void setSource(Map<String, Object> source) {
        this.source = source;
    }

    public Instant getTimestamp() {
        return timestamp;
    }

    public void setTimestamp(Instant timestamp) {
        this.timestamp = timestamp;
    }

    public String getTransactionId() {
        return transactionId;
    }

    public void setTransactionId(String transactionId) {
        this.transactionId = transactionId;
    }

    public String getFullTableName() {
        if (schemaName != null && !schemaName.isEmpty()) {
            return schemaName + "." + tableName;
        }
        return tableName;
    }

    public enum OperationType {
        INSERT,
        UPDATE,
        DELETE,
        READ
    }
}
