package com.dbagent.indexadvisor;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.UUID;

public class IndexSuggestion {

    public enum Priority {
        LOW,
        MEDIUM,
        HIGH
    }

    private String id;
    private String tableName;
    private List<String> columns;
    private String indexName;
    private String createIndexSql;
    private String originalSql;
    private long executionTimeMs;
    private double estimatedSelectivity;
    private double estimatedCardinality;
    private String databaseType;
    private String databaseName;
    private Date suggestionTime;
    private SuggestionStatus status;
    private String approvalToken;
    private String rejectionReason;
    private Priority priority;
    private String reason;

    public enum SuggestionStatus {
        PENDING,
        APPROVED,
        REJECTED,
        EXECUTED,
        FAILED
    }

    public IndexSuggestion() {
        this.id = UUID.randomUUID().toString().replace("-", "").substring(0, 16);
        this.suggestionTime = new Date();
        this.status = SuggestionStatus.PENDING;
        this.columns = new ArrayList<>();
        this.priority = Priority.LOW;
    }

    public String getId() {
        return id;
    }

    public void setId(String id) {
        this.id = id;
    }

    public String getTableName() {
        return tableName;
    }

    public void setTableName(String tableName) {
        this.tableName = tableName;
    }

    public List<String> getColumns() {
        return columns;
    }

    public void setColumns(List<String> columns) {
        this.columns = columns;
    }

    public void addColumn(String column) {
        if (this.columns == null) {
            this.columns = new ArrayList<>();
        }
        this.columns.add(column);
    }

    public String getIndexName() {
        if (indexName == null && tableName != null && columns != null && !columns.isEmpty()) {
            StringBuilder sb = new StringBuilder();
            sb.append("idx_").append(tableName).append("_");
            for (int i = 0; i < columns.size(); i++) {
                if (i > 0) sb.append("_");
                sb.append(columns.get(i));
            }
            return sb.toString();
        }
        return indexName;
    }

    public void setIndexName(String indexName) {
        this.indexName = indexName;
    }

    public String getCreateIndexSql() {
        if (createIndexSql == null && tableName != null && columns != null && !columns.isEmpty()) {
            StringBuilder sb = new StringBuilder();
            sb.append("CREATE INDEX ");
            if (indexName != null) {
                sb.append(indexName);
            } else {
                sb.append("idx_").append(tableName).append("_");
                for (int i = 0; i < columns.size(); i++) {
                    if (i > 0) sb.append("_");
                    sb.append(columns.get(i));
                }
            }
            sb.append(" ON ").append(tableName).append(" (");
            for (int i = 0; i < columns.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(columns.get(i));
            }
            sb.append(")");
            return sb.toString();
        }
        return createIndexSql;
    }

    public void setCreateIndexSql(String createIndexSql) {
        this.createIndexSql = createIndexSql;
    }

    public String getOriginalSql() {
        return originalSql;
    }

    public void setOriginalSql(String originalSql) {
        this.originalSql = originalSql;
    }

    public long getExecutionTimeMs() {
        return executionTimeMs;
    }

    public void setExecutionTimeMs(long executionTimeMs) {
        this.executionTimeMs = executionTimeMs;
    }

    public double getEstimatedSelectivity() {
        return estimatedSelectivity;
    }

    public void setEstimatedSelectivity(double estimatedSelectivity) {
        this.estimatedSelectivity = estimatedSelectivity;
    }

    public double getEstimatedCardinality() {
        return estimatedCardinality;
    }

    public void setEstimatedCardinality(double estimatedCardinality) {
        this.estimatedCardinality = estimatedCardinality;
    }

    public String getDatabaseType() {
        return databaseType;
    }

    public void setDatabaseType(String databaseType) {
        this.databaseType = databaseType;
    }

    public String getDatabaseName() {
        return databaseName;
    }

    public void setDatabaseName(String databaseName) {
        this.databaseName = databaseName;
    }

    public Date getSuggestionTime() {
        return suggestionTime;
    }

    public void setSuggestionTime(Date suggestionTime) {
        this.suggestionTime = suggestionTime;
    }

    public SuggestionStatus getStatus() {
        return status;
    }

    public void setStatus(SuggestionStatus status) {
        this.status = status;
    }

    public String getApprovalToken() {
        return approvalToken;
    }

    public void setApprovalToken(String approvalToken) {
        this.approvalToken = approvalToken;
    }

    public String getRejectionReason() {
        return rejectionReason;
    }

    public void setRejectionReason(String rejectionReason) {
        this.rejectionReason = rejectionReason;
    }

    public Priority getPriority() {
        return priority;
    }

    public void setPriority(Priority priority) {
        this.priority = priority;
    }

    public String getReason() {
        return reason;
    }

    public void setReason(String reason) {
        this.reason = reason;
    }

    @Override
    public String toString() {
        return "IndexSuggestion{" +
                "id='" + id + '\'' +
                ", tableName='" + tableName + '\'' +
                ", columns=" + columns +
                ", indexName='" + getIndexName() + '\'' +
                ", executionTimeMs=" + executionTimeMs +
                ", estimatedSelectivity=" + estimatedSelectivity +
                ", priority=" + priority +
                ", status=" + status +
                '}';
    }
}
