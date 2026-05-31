package com.cdc.common.event;

import java.time.Instant;

public class TableSyncStatus {

    private String fullTableName;
    private SyncStatus status;
    private Instant pausedAt;
    private String pausedReason;
    private DdlEvent pendingDdlEvent;
    private Instant lastSyncedAt;
    private long lastProcessedOffset;
    private String schemaSnapshot;

    public String getFullTableName() {
        return fullTableName;
    }

    public void setFullTableName(String fullTableName) {
        this.fullTableName = fullTableName;
    }

    public SyncStatus getStatus() {
        return status;
    }

    public void setStatus(SyncStatus status) {
        this.status = status;
    }

    public Instant getPausedAt() {
        return pausedAt;
    }

    public void setPausedAt(Instant pausedAt) {
        this.pausedAt = pausedAt;
    }

    public String getPausedReason() {
        return pausedReason;
    }

    public void setPausedReason(String pausedReason) {
        this.pausedReason = pausedReason;
    }

    public DdlEvent getPendingDdlEvent() {
        return pendingDdlEvent;
    }

    public void setPendingDdlEvent(DdlEvent pendingDdlEvent) {
        this.pendingDdlEvent = pendingDdlEvent;
    }

    public Instant getLastSyncedAt() {
        return lastSyncedAt;
    }

    public void setLastSyncedAt(Instant lastSyncedAt) {
        this.lastSyncedAt = lastSyncedAt;
    }

    public long getLastProcessedOffset() {
        return lastProcessedOffset;
    }

    public void setLastProcessedOffset(long lastProcessedOffset) {
        this.lastProcessedOffset = lastProcessedOffset;
    }

    public String getSchemaSnapshot() {
        return schemaSnapshot;
    }

    public void setSchemaSnapshot(String schemaSnapshot) {
        this.schemaSnapshot = schemaSnapshot;
    }

    public boolean isRunning() {
        return status == SyncStatus.RUNNING;
    }

    public boolean isPaused() {
        return status == SyncStatus.PAUSED || status == SyncStatus.PAUSED_FOR_DDL;
    }

    public void resume() {
        this.status = SyncStatus.RUNNING;
        this.pausedAt = null;
        this.pausedReason = null;
        this.pendingDdlEvent = null;
    }

    public void pause(String reason) {
        this.status = SyncStatus.PAUSED;
        this.pausedAt = Instant.now();
        this.pausedReason = reason;
    }

    public void pauseForDdl(DdlEvent ddlEvent, String reason) {
        this.status = SyncStatus.PAUSED_FOR_DDL;
        this.pausedAt = Instant.now();
        this.pausedReason = reason;
        this.pendingDdlEvent = ddlEvent;
    }

    public enum SyncStatus {
        RUNNING,
        PAUSED,
        PAUSED_FOR_DDL,
        ERROR,
        STOPPED
    }
}
