package com.cdc.core.event;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.config.TableFilterConfig;
import com.cdc.common.event.CdcEvent;
import com.cdc.common.event.DdlEvent;
import com.cdc.common.event.DdlEvent.DdlStatus;
import com.cdc.common.event.TableSchema;
import com.cdc.common.util.TableNameUtil;
import com.cdc.core.alert.DingTalkAlertService;
import com.cdc.core.ddl.DdlPolicyEngine;
import com.cdc.core.kafka.KafkaProducerManager;
import com.cdc.core.masking.MaskingRuleEngine;
import com.cdc.core.status.TableSyncStatusManager;
import com.cdc.metrics.MetricsManager;
import io.debezium.engine.ChangeEvent;
import io.debezium.engine.DebeziumEngine;
import io.prometheus.client.Histogram;
import org.apache.kafka.connect.data.Struct;
import org.apache.kafka.connect.source.SourceRecord;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

public class CdcChangeConsumer implements DebeziumEngine.ChangeConsumer<ChangeEvent<SourceRecord, SourceRecord>> {

    private static final Logger logger = LoggerFactory.getLogger(CdcChangeConsumer.class);

    private final CdcConfig config;
    private final KafkaProducerManager kafkaProducer;
    private final MetricsManager metricsManager;
    private final TableSyncStatusManager statusManager;
    private final DdlPolicyEngine ddlPolicyEngine;
    private final DingTalkAlertService alertService;
    private final MaskingRuleEngine maskingRuleEngine;
    private final Set<String> registeredTables = ConcurrentHashMap.newKeySet();
    private final Map<String, TableSchema> tableSchemaCache = new ConcurrentHashMap<>();
    private final AtomicBoolean running = new AtomicBoolean(true);
    private final String dbType;

    public CdcChangeConsumer(CdcConfig config, KafkaProducerManager kafkaProducer,
                             MetricsManager metricsManager, TableSyncStatusManager statusManager,
                             DdlPolicyEngine ddlPolicyEngine, DingTalkAlertService alertService,
                             MaskingRuleEngine maskingRuleEngine) {
        this.config = config;
        this.kafkaProducer = kafkaProducer;
        this.metricsManager = metricsManager;
        this.statusManager = statusManager;
        this.ddlPolicyEngine = ddlPolicyEngine;
        this.alertService = alertService;
        this.maskingRuleEngine = maskingRuleEngine;
        this.dbType = config.getDatabase().getType().name().toLowerCase();
    }

    @Override
    public void handleBatch(List<ChangeEvent<SourceRecord, SourceRecord>> events,
                            DebeziumEngine.RecordCommitter<ChangeEvent<SourceRecord, SourceRecord>> committer) throws InterruptedException {
        for (ChangeEvent<SourceRecord, SourceRecord> event : events) {
            if (!running.get()) {
                break;
            }

            try {
                if (DebeziumEventConverter.isSchemaChangeEvent(event.value())) {
                    handleSchemaChangeEvent(event.value());
                } else {
                    handleDataEvent(event.value());
                }
                committer.markProcessed(event);
            } catch (Exception e) {
                logger.error("Failed to handle event: {}", e.getMessage(), e);
                SourceRecord record = event.value();
                if (record != null && record.value() != null) {
                    Struct value = (Struct) record.value();
                    Struct source = value.getStruct("source");
                    if (source != null) {
                        String table = source.getString("schema") + "." + source.getString("table");
                        metricsManager.recordFailedEvent(source.getString("db"), table);
                    }
                }
                handleDdlFailure(event.value(), e);
            }
        }
        committer.markBatchFinished();
        kafkaProducer.flush();

        if (metricsManager != null) {
            metricsManager.setKafkaProducerQueueSize(kafkaProducer.getQueueSize());
        }
    }

    private void handleSchemaChangeEvent(SourceRecord sourceRecord) {
        DdlEvent ddlEvent = DebeziumEventConverter.convertDdl(sourceRecord, dbType);
        if (ddlEvent == null) {
            return;
        }

        String fullTableName = ddlEvent.getFullTableName();
        logger.info("Processing DDL event for table: {}, type: {}", fullTableName, ddlEvent.getDdlType());

        try {
            DdlStatus status = ddlPolicyEngine.processDdlEvent(ddlEvent);

            switch (status) {
                case APPLIED:
                case SKIPPED:
                    refreshTableSchema(fullTableName);
                    break;
                case MANUAL_REQUIRED:
                    logger.warn("Table {} requires manual intervention, sync paused", fullTableName);
                    break;
                case FAILED:
                case PENDING:
                default:
                    logger.error("DDL event processing resulted in status: {} for table {}", status, fullTableName);
                    break;
            }
        } catch (Exception e) {
            logger.error("Failed to process DDL event for table: {}", fullTableName, e);
            handleDdlFailure(sourceRecord, e);
        }
    }

    private void handleDdlFailure(SourceRecord sourceRecord, Exception e) {
        String fullTableName = extractTableName(sourceRecord);
        if (fullTableName == null) {
            return;
        }

        if (config.getDdl().isPauseTableOnFailure()) {
            statusManager.pauseTable(fullTableName,
                    String.format("DDL processing failed: %s", e.getMessage()));

            if (alertService != null && alertService.isEnabled()) {
                try {
                    alertService.sendAlert("CDC DDL Processing Failed",
                            String.format("Table %s sync paused due to DDL processing failure.\n\nError: %s\n\nTable has been paused. Review and resume using CLI.",
                                    fullTableName, e.getMessage()));
                } catch (Exception alertEx) {
                    logger.error("Failed to send DDL failure alert", alertEx);
                }
            }
        }
    }

    private String extractTableName(SourceRecord sourceRecord) {
        if (sourceRecord == null || sourceRecord.value() == null) {
            return null;
        }
        Struct value = (Struct) sourceRecord.value();
        Struct source = value.getStruct("source");
        if (source != null) {
            String schema = source.getString("schema");
            String table = source.getString("table");
            if (schema != null && table != null) {
                return schema + "." + table;
            }
            return table;
        }
        return null;
    }

    private void handleDataEvent(SourceRecord sourceRecord) {
        if (sourceRecord == null || sourceRecord.value() == null) {
            return;
        }

        CdcEvent cdcEvent = DebeziumEventConverter.convert(sourceRecord);
        if (cdcEvent == null) {
            return;
        }

        String fullTableName = cdcEvent.getFullTableName();

        if (statusManager.isTablePaused(fullTableName)) {
            logger.debug("Skipping event for paused table: {}", fullTableName);
            return;
        }

        if (!shouldProcessTable(fullTableName)) {
            logger.trace("Skipping filtered table: {}", fullTableName);
            return;
        }

        if (maskingRuleEngine != null && config.getDataMasking() != null && config.getDataMasking().isEnabled()) {
            cdcEvent = maskingRuleEngine.processEvent(cdcEvent);
            if (cdcEvent == null) {
                logger.debug("Row filtered out by masking rules for table: {}", fullTableName);
                return;
            }
        }

        if (!registeredTables.contains(fullTableName)) {
            registerTableSchema(fullTableName, sourceRecord);
        }

        Histogram.Timer timer = null;
        if (metricsManager != null) {
            timer = metricsManager.startProcessingTimer(cdcEvent.getSourceDatabase(), cdcEvent.getTableName());
            metricsManager.recordEvent(cdcEvent.getSourceDatabase(), cdcEvent.getTableName(),
                    cdcEvent.getOperation().name());

            if (cdcEvent.getTimestamp() != null) {
                metricsManager.recordEventLag(cdcEvent.getSourceDatabase(), cdcEvent.getTableName(),
                        cdcEvent.getTimestamp().toEpochMilli());
            }
        }

        kafkaProducer.send(cdcEvent, timer);

        if (logger.isDebugEnabled()) {
            logger.debug("Processed {} event for table {} with key columns: {}",
                    cdcEvent.getOperation(), fullTableName,
                    cdcEvent.getAfter() != null ? cdcEvent.getAfter().keySet() : "N/A");
        }
    }

    private boolean shouldProcessTable(String fullTableName) {
        if (config.getTableFilters() == null || config.getTableFilters().isEmpty()) {
            return true;
        }

        for (TableFilterConfig filter : config.getTableFilters()) {
            if (filter.getTableName().equals(fullTableName) ||
                    filter.getTableName().endsWith("." + fullTableName) ||
                    fullTableName.endsWith("." + filter.getTableName())) {
                return true;
            }
        }
        return false;
    }

    private void registerTableSchema(String fullTableName, SourceRecord sourceRecord) {
        synchronized (registeredTables) {
            if (registeredTables.contains(fullTableName)) {
                return;
            }

            TableSchema tableSchema = DebeziumEventConverter.extractTableSchema(sourceRecord);
            if (tableSchema != null) {
                kafkaProducer.registerTableSchema(tableSchema);
                registeredTables.add(fullTableName);
                tableSchemaCache.put(fullTableName, tableSchema);
                logger.info("Registered schema for table: {} with {} columns",
                        fullTableName, tableSchema.getColumns().size());

                for (TableSchema.ColumnSchema column : tableSchema.getColumns()) {
                    logger.debug("  Column: {} ({}, nullable: {})",
                            column.getName(), column.getType(), column.isNullable());
                }
            }
        }
    }

    private void refreshTableSchema(String fullTableName) {
        if (!registeredTables.contains(fullTableName)) {
            logger.debug("Table {} not registered, skipping schema refresh", fullTableName);
            return;
        }

        logger.info("Refreshing schema for table: {}", fullTableName);
        registeredTables.remove(fullTableName);
        tableSchemaCache.remove(fullTableName);
    }

    public void stop() {
        running.set(false);
    }

    public Set<String> getRegisteredTables() {
        return registeredTables;
    }

    public TableSyncStatusManager getStatusManager() {
        return statusManager;
    }
}
