package com.cdc.cli.model;

import java.util.ArrayList;
import java.util.List;

public class ValidationResult {

    private String tableName;
    private boolean passed;
    private long sourceRowCount;
    private long targetRowCount;
    private long rowCountDiff;
    private int sampleSize;
    private int mismatchedSamples;
    private List<RowMismatch> mismatches = new ArrayList<>();
    private long durationMs;
    private String errorMessage;

    public String getTableName() {
        return tableName;
    }

    public void setTableName(String tableName) {
        this.tableName = tableName;
    }

    public boolean isPassed() {
        return passed;
    }

    public void setPassed(boolean passed) {
        this.passed = passed;
    }

    public long getSourceRowCount() {
        return sourceRowCount;
    }

    public void setSourceRowCount(long sourceRowCount) {
        this.sourceRowCount = sourceRowCount;
    }

    public long getTargetRowCount() {
        return targetRowCount;
    }

    public void setTargetRowCount(long targetRowCount) {
        this.targetRowCount = targetRowCount;
    }

    public long getRowCountDiff() {
        return rowCountDiff;
    }

    public void setRowCountDiff(long rowCountDiff) {
        this.rowCountDiff = rowCountDiff;
    }

    public int getSampleSize() {
        return sampleSize;
    }

    public void setSampleSize(int sampleSize) {
        this.sampleSize = sampleSize;
    }

    public int getMismatchedSamples() {
        return mismatchedSamples;
    }

    public void setMismatchedSamples(int mismatchedSamples) {
        this.mismatchedSamples = mismatchedSamples;
    }

    public List<RowMismatch> getMismatches() {
        return mismatches;
    }

    public void setMismatches(List<RowMismatch> mismatches) {
        this.mismatches = mismatches;
    }

    public void addMismatch(RowMismatch mismatch) {
        this.mismatches.add(mismatch);
    }

    public long getDurationMs() {
        return durationMs;
    }

    public void setDurationMs(long durationMs) {
        this.durationMs = durationMs;
    }

    public String getErrorMessage() {
        return errorMessage;
    }

    public void setErrorMessage(String errorMessage) {
        this.errorMessage = errorMessage;
    }

    public static class RowMismatch {
        private String primaryKey;
        private String columnName;
        private Object sourceValue;
        private Object targetValue;

        public RowMismatch(String primaryKey, String columnName, Object sourceValue, Object targetValue) {
            this.primaryKey = primaryKey;
            this.columnName = columnName;
            this.sourceValue = sourceValue;
            this.targetValue = targetValue;
        }

        public String getPrimaryKey() {
            return primaryKey;
        }

        public String getColumnName() {
            return columnName;
        }

        public Object getSourceValue() {
            return sourceValue;
        }

        public Object getTargetValue() {
            return targetValue;
        }

        @Override
        public String toString() {
            return String.format("PK=%s, Column=%s, Source=%s, Target=%s",
                    primaryKey, columnName, sourceValue, targetValue);
        }
    }
}
