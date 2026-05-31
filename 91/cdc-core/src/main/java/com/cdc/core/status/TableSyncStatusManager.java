package com.cdc.core.status;

import com.cdc.common.event.DdlEvent;
import com.cdc.common.event.TableSyncStatus;
import com.cdc.common.event.TableSyncStatus.SyncStatus;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Collection;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class TableSyncStatusManager {

    private static final Logger logger = LoggerFactory.getLogger(TableSyncStatusManager.class);

    private final Map<String, TableSyncStatus> statusMap = new ConcurrentHashMap<>();
    private final String stateStorePath;
    private final ObjectMapper objectMapper;

    public TableSyncStatusManager(String stateStorePath) {
        this.stateStorePath = stateStorePath;
        this.objectMapper = new ObjectMapper();
        this.objectMapper.registerModule(new JavaTimeModule());
        this.objectMapper.disable(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS);
        this.objectMapper.enable(SerializationFeature.INDENT_OUTPUT);

        loadState();
    }

    public TableSyncStatus getOrCreateStatus(String fullTableName) {
        return statusMap.computeIfAbsent(fullTableName, key -> {
            TableSyncStatus status = new TableSyncStatus();
            status.setFullTableName(key);
            status.setStatus(SyncStatus.RUNNING);
            return status;
        });
    }

    public TableSyncStatus getStatus(String fullTableName) {
        return statusMap.get(fullTableName);
    }

    public Collection<TableSyncStatus> getAllStatuses() {
        return statusMap.values();
    }

    public void pauseTable(String fullTableName, String reason) {
        TableSyncStatus status = getOrCreateStatus(fullTableName);
        status.pause(reason);
        saveState();
        logger.info("Table {} paused: {}", fullTableName, reason);
    }

    public void pauseForDdl(String fullTableName, DdlEvent ddlEvent, String reason) {
        TableSyncStatus status = getOrCreateStatus(fullTableName);
        status.pauseForDdl(ddlEvent, reason);
        saveState();
        logger.warn("Table {} paused for DDL: {}", fullTableName, reason);
    }

    public void resumeTable(String fullTableName) {
        TableSyncStatus status = statusMap.get(fullTableName);
        if (status != null) {
            status.resume();
            saveState();
            logger.info("Table {} resumed", fullTableName);
        }
    }

    public void markTableDropped(String fullTableName) {
        TableSyncStatus status = getOrCreateStatus(fullTableName);
        status.setStatus(SyncStatus.STOPPED);
        saveState();
        logger.info("Table {} marked as dropped", fullTableName);
    }

    public void updateLastSyncInfo(String fullTableName, long offset) {
        TableSyncStatus status = getOrCreateStatus(fullTableName);
        status.setLastSyncedAt(java.time.Instant.now());
        status.setLastProcessedOffset(offset);
        saveState();
    }

    public boolean isTableRunning(String fullTableName) {
        TableSyncStatus status = statusMap.get(fullTableName);
        return status == null || status.isRunning();
    }

    public boolean isTablePaused(String fullTableName) {
        TableSyncStatus status = statusMap.get(fullTableName);
        return status != null && status.isPaused();
    }

    public Map<String, Object> getStatusSummary() {
        Map<String, Object> summary = new ConcurrentHashMap<>();
        int running = 0;
        int paused = 0;
        int pausedForDdl = 0;
        int stopped = 0;

        for (TableSyncStatus status : statusMap.values()) {
            switch (status.getStatus()) {
                case RUNNING:
                    running++;
                    break;
                case PAUSED:
                    paused++;
                    break;
                case PAUSED_FOR_DDL:
                    pausedForDdl++;
                    break;
                case STOPPED:
                case ERROR:
                    stopped++;
                    break;
            }
        }

        summary.put("total", statusMap.size());
        summary.put("running", running);
        summary.put("paused", paused);
        summary.put("pausedForDdl", pausedForDdl);
        summary.put("stopped", stopped);
        summary.put("tables", statusMap.values());

        return summary;
    }

    private void loadState() {
        Path path = Paths.get(stateStorePath);
        if (!Files.exists(path)) {
            logger.info("No existing state file found at {}", stateStorePath);
            return;
        }

        try {
            String content = Files.readString(path);
            if (content.isEmpty()) {
                return;
            }

            Map<String, TableSyncStatus> loaded = objectMapper.readValue(content,
                    objectMapper.getTypeFactory().constructMapType(
                            ConcurrentHashMap.class, String.class, TableSyncStatus.class));
            statusMap.putAll(loaded);
            logger.info("Loaded {} table statuses from {}", statusMap.size(), stateStorePath);
        } catch (IOException e) {
            logger.error("Failed to load state from {}", stateStorePath, e);
        }
    }

    public synchronized void saveState() {
        try {
            Path path = Paths.get(stateStorePath);
            Path parentDir = path.getParent();
            if (parentDir != null && !Files.exists(parentDir)) {
                Files.createDirectories(parentDir);
            }

            String content = objectMapper.writeValueAsString(statusMap);
            Files.writeString(path, content);
            logger.debug("State saved to {}", stateStorePath);
        } catch (IOException e) {
            logger.error("Failed to save state to {}", stateStorePath, e);
        }
    }
}
