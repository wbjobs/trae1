package com.cdc.core.ddl;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.config.DdlConfig;
import com.cdc.common.config.DdlConfig.DdlPolicy;
import com.cdc.common.event.DdlEvent;
import com.cdc.common.event.DdlEvent.DdlStatus;
import com.cdc.common.event.DdlEvent.DdlType;
import com.cdc.common.event.TableSyncStatus;
import com.cdc.core.alert.DingTalkAlertService;
import com.cdc.core.status.TableSyncStatusManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class DdlPolicyEngine {

    private static final Logger logger = LoggerFactory.getLogger(DdlPolicyEngine.class);

    private final DdlConfig ddlConfig;
    private final TableSyncStatusManager statusManager;
    private final DingTalkAlertService alertService;

    public DdlPolicyEngine(CdcConfig config, TableSyncStatusManager statusManager,
                           DingTalkAlertService alertService) {
        this.ddlConfig = config.getDdl();
        this.statusManager = statusManager;
        this.alertService = alertService;
    }

    public DdlStatus processDdlEvent(DdlEvent event) {
        String fullTableName = event.getFullTableName();
        logger.info("Processing DDL event for table: {}, type: {}", fullTableName, event.getDdlType());

        DdlPolicy policy = ddlConfig.getPolicy();
        switch (policy) {
            case SKIP:
                return handleSkipPolicy(event);
            case MANUAL:
                return handleManualPolicy(event);
            case AUTO:
            default:
                return handleAutoPolicy(event);
        }
    }

    private DdlStatus handleSkipPolicy(DdlEvent event) {
        logger.warn("DDL policy is SKIP, skipping DDL event for table: {}", event.getFullTableName());
        event.setStatus(DdlStatus.SKIPPED);

        sendAlert(event, "DDL Event Skipped",
                String.format("DDL event skipped for table %s:\n%s", event.getFullTableName(), event.getRawSql()));

        return DdlStatus.SKIPPED;
    }

    private DdlStatus handleManualPolicy(DdlEvent event) {
        logger.info("DDL policy is MANUAL, pausing table: {}", event.getFullTableName());
        event.setStatus(DdlStatus.MANUAL_REQUIRED);

        String reason = String.format("Manual DDL handling required: %s", event.getRawSql());
        statusManager.pauseForDdl(event.getFullTableName(), event, reason);

        sendAlert(event, "DDL Manual Intervention Required",
                String.format("Table %s has been paused due to DDL event.\n\nDDL: %s\n\nAction required: Review and resume using CLI.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.MANUAL_REQUIRED;
    }

    private DdlStatus handleAutoPolicy(DdlEvent event) {
        logger.info("DDL policy is AUTO, automatically processing DDL for table: {}", event.getFullTableName());

        switch (event.getDdlType()) {
            case ADD_COLUMN:
                return handleAddColumn(event);
            case ALTER_COLUMN_TYPE:
                return handleAlterColumnType(event);
            case DROP_COLUMN:
                return handleDropColumn(event);
            case RENAME_COLUMN:
                return handleRenameColumn(event);
            case CREATE_TABLE:
                return handleCreateTable(event);
            case DROP_TABLE:
                return handleDropTable(event);
            case ALTER_TABLE:
            case OTHER:
            default:
                return handleUnknownDdl(event);
        }
    }

    private DdlStatus handleAddColumn(DdlEvent event) {
        logger.info("Automatically handling ADD COLUMN for table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.APPLIED);

        sendAlert(event, "DDL - Add Column Applied",
                String.format("Automatically applied ADD COLUMN for table %s:\n%s",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.APPLIED;
    }

    private DdlStatus handleAlterColumnType(DdlEvent event) {
        logger.info("Handling ALTER COLUMN TYPE for table: {}", event.getFullTableName());

        if (event.getColumnChanges().isEmpty()) {
            logger.warn("No column changes found in ALTER COLUMN TYPE event");
            return handleUnknownDdl(event);
        }

        boolean allSafe = true;
        for (var change : event.getColumnChanges()) {
            if (change.getChangeType() == DdlEvent.ColumnChangeType.MODIFY_TYPE) {
                boolean safe = DdlEventParser.isSafeTypeConversion(change.getOldType(), change.getNewType());
                if (!safe) {
                    allSafe = false;
                    logger.warn("Unsafe type conversion detected: {} -> {} for column {}",
                            change.getOldType(), change.getNewType(), change.getColumnName());
                }
            }
        }

        if (allSafe && ddlConfig.isAutoApplySafeConversions()) {
            event.setStatus(DdlStatus.APPLIED);
            sendAlert(event, "DDL - Safe Column Type Change Applied",
                    String.format("Automatically applied safe column type change for table %s:\n%s",
                            event.getFullTableName(), event.getRawSql()));
            return DdlStatus.APPLIED;
        } else {
            event.setStatus(DdlStatus.MANUAL_REQUIRED);
            String reason = String.format("Unsafe type conversion requires manual review: %s", event.getRawSql());
            statusManager.pauseForDdl(event.getFullTableName(), event, reason);

            sendAlert(event, "DDL - Unsafe Column Type Change Detected",
                    String.format("Table %s paused due to unsafe column type change.\n\nDDL: %s\n\nPlease review and resume using CLI.",
                            event.getFullTableName(), event.getRawSql()));
            return DdlStatus.MANUAL_REQUIRED;
        }
    }

    private DdlStatus handleDropColumn(DdlEvent event) {
        logger.info("Automatically handling DROP COLUMN for table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.APPLIED);

        sendAlert(event, "DDL - Drop Column Applied",
                String.format("Automatically applied DROP COLUMN for table %s:\n%s\n\nNote: Column will be ignored in future sync.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.APPLIED;
    }

    private DdlStatus handleRenameColumn(DdlEvent event) {
        logger.info("Handling RENAME COLUMN for table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.MANUAL_REQUIRED);
        String reason = String.format("Column rename requires manual handling: %s", event.getRawSql());
        statusManager.pauseForDdl(event.getFullTableName(), event, reason);

        sendAlert(event, "DDL - Column Rename Detected",
                String.format("Table %s paused due to column rename.\n\nDDL: %s\n\nAction required: Update consumer to use new column name and resume.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.MANUAL_REQUIRED;
    }

    private DdlStatus handleCreateTable(DdlEvent event) {
        logger.info("Handling CREATE TABLE for table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.APPLIED);

        sendAlert(event, "DDL - New Table Created",
                String.format("New table detected: %s\n\nDDL: %s\n\nNote: Table will be included in sync if matching filters.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.APPLIED;
    }

    private DdlStatus handleDropTable(DdlEvent event) {
        logger.info("Handling DROP TABLE for table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.APPLIED);
        statusManager.markTableDropped(event.getFullTableName());

        sendAlert(event, "DDL - Table Dropped",
                String.format("Table dropped: %s\n\nDDL: %s\n\nNote: Sync has been stopped for this table.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.APPLIED;
    }

    private DdlStatus handleUnknownDdl(DdlEvent event) {
        logger.warn("Unknown DDL type, pausing table: {}", event.getFullTableName());

        event.setStatus(DdlStatus.MANUAL_REQUIRED);
        String reason = String.format("Unknown DDL type requires manual review: %s", event.getRawSql());
        statusManager.pauseForDdl(event.getFullTableName(), event, reason);

        sendAlert(event, "DDL - Unknown Type Detected",
                String.format("Table %s paused due to unknown DDL type.\n\nDDL: %s\n\nAction required: Review and resume using CLI.",
                        event.getFullTableName(), event.getRawSql()));

        return DdlStatus.MANUAL_REQUIRED;
    }

    private void sendAlert(DdlEvent event, String title, String message) {
        if (alertService != null && ddlConfig.getAlert() != null && ddlConfig.getAlert().isEnabled()) {
            try {
                alertService.sendAlert(title, message);
            } catch (Exception e) {
                logger.error("Failed to send alert for DDL event: {}", event.getFullTableName(), e);
            }
        }
    }
}
