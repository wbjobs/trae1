package com.cdc.common.event;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;

public class DdlEvent {

    private String sourceDatabase;
    private String schemaName;
    private String tableName;
    private DdlType ddlType;
    private String rawSql;
    private List<ColumnChange> columnChanges = new ArrayList<>();
    private Instant timestamp;
    private DdlStatus status = DdlStatus.PENDING;
    private String errorMessage;

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

    public DdlType getDdlType() {
        return ddlType;
    }

    public void setDdlType(DdlType ddlType) {
        this.ddlType = ddlType;
    }

    public String getRawSql() {
        return rawSql;
    }

    public void setRawSql(String rawSql) {
        this.rawSql = rawSql;
    }

    public List<ColumnChange> getColumnChanges() {
        return columnChanges;
    }

    public void setColumnChanges(List<ColumnChange> columnChanges) {
        this.columnChanges = columnChanges;
    }

    public void addColumnChange(ColumnChange change) {
        this.columnChanges.add(change);
    }

    public Instant getTimestamp() {
        return timestamp;
    }

    public void setTimestamp(Instant timestamp) {
        this.timestamp = timestamp;
    }

    public DdlStatus getStatus() {
        return status;
    }

    public void setStatus(DdlStatus status) {
        this.status = status;
    }

    public String getErrorMessage() {
        return errorMessage;
    }

    public void setErrorMessage(String errorMessage) {
        this.errorMessage = errorMessage;
    }

    public String getFullTableName() {
        if (schemaName != null && !schemaName.isEmpty()) {
            return schemaName + "." + tableName;
        }
        return tableName;
    }

    public enum DdlType {
        ADD_COLUMN,
        ALTER_COLUMN_TYPE,
        DROP_COLUMN,
        RENAME_COLUMN,
        CREATE_TABLE,
        DROP_TABLE,
        ALTER_TABLE,
        OTHER
    }

    public enum DdlStatus {
        PENDING,
        APPLIED,
        FAILED,
        SKIPPED,
        MANUAL_REQUIRED
    }

    public static class ColumnChange {
        private String columnName;
        private String oldColumnName;
        private String oldType;
        private String newType;
        private Integer oldPosition;
        private Integer newPosition;
        private Boolean oldNullable;
        private Boolean newNullable;
        private String defaultValue;
        private ColumnChangeType changeType;

        public String getColumnName() {
            return columnName;
        }

        public void setColumnName(String columnName) {
            this.columnName = columnName;
        }

        public String getOldColumnName() {
            return oldColumnName;
        }

        public void setOldColumnName(String oldColumnName) {
            this.oldColumnName = oldColumnName;
        }

        public String getOldType() {
            return oldType;
        }

        public void setOldType(String oldType) {
            this.oldType = oldType;
        }

        public String getNewType() {
            return newType;
        }

        public void setNewType(String newType) {
            this.newType = newType;
        }

        public Integer getOldPosition() {
            return oldPosition;
        }

        public void setOldPosition(Integer oldPosition) {
            this.oldPosition = oldPosition;
        }

        public Integer getNewPosition() {
            return newPosition;
        }

        public void setNewPosition(Integer newPosition) {
            this.newPosition = newPosition;
        }

        public Boolean getOldNullable() {
            return oldNullable;
        }

        public void setOldNullable(Boolean oldNullable) {
            this.oldNullable = oldNullable;
        }

        public Boolean getNewNullable() {
            return newNullable;
        }

        public void setNewNullable(Boolean newNullable) {
            this.newNullable = newNullable;
        }

        public String getDefaultValue() {
            return defaultValue;
        }

        public void setDefaultValue(String defaultValue) {
            this.defaultValue = defaultValue;
        }

        public ColumnChangeType getChangeType() {
            return changeType;
        }

        public void setChangeType(ColumnChangeType changeType) {
            this.changeType = changeType;
        }

        public enum ColumnChangeType {
            ADD,
            DROP,
            MODIFY_TYPE,
            MODIFY_NULLABLE,
            RENAME,
            MODIFY_POSITION
        }
    }
}
