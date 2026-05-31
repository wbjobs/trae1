package com.cdc.cli.command;

import com.cdc.common.event.TableSyncStatus;
import com.cdc.common.event.TableSyncStatus.SyncStatus;
import com.cdc.core.status.TableSyncStatusManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import picocli.CommandLine;

import java.util.Collection;
import java.util.concurrent.Callable;

@CommandLine.Command(
        name = "table-status",
        description = "View and manage table sync status",
        mixinStandardHelpOptions = true
)
public class TableStatusCommand implements Callable<Integer> {

    private static final Logger logger = LoggerFactory.getLogger(TableStatusCommand.class);

    @CommandLine.Option(names = {"--state-store"}, description = "Path to state store file", defaultValue = "./data/ddl-state")
    private String stateStorePath;

    @CommandLine.Option(names = {"--table"}, description = "Specific table to show status")
    private String tableName;

    @CommandLine.Option(names = {"--resume"}, description = "Resume a paused table")
    private boolean resume;

    @CommandLine.Option(names = {"--pause"}, description = "Pause a table")
    private boolean pause;

    @CommandLine.Option(names = {"--reason"}, description = "Reason for pausing")
    private String reason;

    @CommandLine.Option(names = {"--output-format"}, description = "Output format: text or json", defaultValue = "text")
    private String outputFormat;

    @Override
    public Integer call() {
        try {
            TableSyncStatusManager statusManager = new TableSyncStatusManager(stateStorePath);

            if (resume && tableName != null) {
                return handleResume(statusManager, tableName);
            }

            if (pause && tableName != null) {
                return handlePause(statusManager, tableName, reason);
            }

            if (tableName != null) {
                return showTableStatus(statusManager, tableName);
            }

            return showAllStatuses(statusManager);

        } catch (Exception e) {
            logger.error("Failed to execute table-status command", e);
            System.err.println("Error: " + e.getMessage());
            return 2;
        }
    }

    private Integer handleResume(TableSyncStatusManager statusManager, String tableName) {
        TableSyncStatus status = statusManager.getStatus(tableName);
        if (status == null) {
            System.err.println("Table not found: " + tableName);
            return 1;
        }

        if (status.isRunning()) {
            System.out.println("Table is already running: " + tableName);
            return 0;
        }

        statusManager.resumeTable(tableName);
        System.out.println("Table resumed: " + tableName);
        printStatus(statusManager.getStatus(tableName));
        return 0;
    }

    private Integer handlePause(TableSyncStatusManager statusManager, String tableName, String reason) {
        TableSyncStatus status = statusManager.getStatus(tableName);
        if (status == null) {
            System.err.println("Table not found: " + tableName);
            return 1;
        }

        if (status.isPaused()) {
            System.out.println("Table is already paused: " + tableName);
            return 0;
        }

        String pauseReason = reason != null ? reason : "Manually paused via CLI";
        statusManager.pauseTable(tableName, pauseReason);
        System.out.println("Table paused: " + tableName);
        printStatus(statusManager.getStatus(tableName));
        return 0;
    }

    private Integer showTableStatus(TableSyncStatusManager statusManager, String tableName) {
        TableSyncStatus status = statusManager.getStatus(tableName);
        if (status == null) {
            System.err.println("Table not found: " + tableName);
            return 1;
        }

        printStatus(status);
        return 0;
    }

    private Integer showAllStatuses(TableSyncStatusManager statusManager) {
        Collection<TableSyncStatus> statuses = statusManager.getAllStatuses();

        if (statuses.isEmpty()) {
            System.out.println("No tables tracked.");
            return 0;
        }

        System.out.println("=".repeat(80));
        System.out.printf("%-40s %-20s %-20s%n", "TABLE", "STATUS", "LAST SYNC");
        System.out.println("-".repeat(80));

        for (TableSyncStatus status : statuses) {
            System.out.printf("%-40s %-20s %-20s%n",
                    status.getFullTableName(),
                    formatStatus(status.getStatus()),
                    status.getLastSyncedAt() != null ? status.getLastSyncedAt().toString() : "N/A"
            );
        }

        System.out.println("=".repeat(80));

        long running = statuses.stream().filter(s -> s.getStatus() == SyncStatus.RUNNING).count();
        long paused = statuses.stream().filter(s -> s.isPaused()).count();
        long stopped = statuses.stream().filter(s -> s.getStatus() == SyncStatus.STOPPED).count();

        System.out.printf("Total: %d, Running: %d, Paused: %d, Stopped: %d%n",
                statuses.size(), running, paused, stopped);

        return 0;
    }

    private void printStatus(TableSyncStatus status) {
        System.out.println("=".repeat(60));
        System.out.println("Table: " + status.getFullTableName());
        System.out.println("Status: " + formatStatus(status.getStatus()));
        System.out.println("Last Synced: " + (status.getLastSyncedAt() != null ? status.getLastSyncedAt() : "N/A"));
        System.out.println("Last Offset: " + status.getLastProcessedOffset());

        if (status.getPausedAt() != null) {
            System.out.println("Paused At: " + status.getPausedAt());
        }
        if (status.getPausedReason() != null) {
            System.out.println("Paused Reason: " + status.getPausedReason());
        }
        if (status.getPendingDdlEvent() != null) {
            System.out.println("Pending DDL: " + status.getPendingDdlEvent().getDdlType());
            System.out.println("DDL SQL: " + status.getPendingDdlEvent().getRawSql());
        }
        System.out.println("=".repeat(60));
    }

    private String formatStatus(SyncStatus status) {
        switch (status) {
            case RUNNING:
                return "RUNNING";
            case PAUSED:
                return "PAUSED";
            case PAUSED_FOR_DDL:
                return "PAUSED (DDL)";
            case STOPPED:
                return "STOPPED";
            case ERROR:
                return "ERROR";
            default:
                return "UNKNOWN";
        }
    }
}
