package com.alibaba.polardb.index.canal;

import com.alibaba.otter.canal.protocol.CanalEntry;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.model.event.DataChangeEvent;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;

public class BinlogEventParser {

    private static final Logger logger = LoggerFactory.getLogger(BinlogEventParser.class);

    public interface DdlEventListener {
        void onDdlEvent(String shardId, CanalEntry.EventType eventType, String sql);
    }

    private final ShardConfig shardConfig;
    private DdlEventListener ddlEventListener;

    public BinlogEventParser(ShardConfig shardConfig) {
        this.shardConfig = shardConfig;
    }

    public void setDdlEventListener(DdlEventListener listener) {
        this.ddlEventListener = listener;
    }

    public List<DataChangeEvent> parse(CanalEntry.Entry entry, long receiveTime) {
        List<DataChangeEvent> events = new ArrayList<>();

        try {
            if (entry.getEntryType() == CanalEntry.EntryType.TRANSACTIONBEGIN
                    || entry.getEntryType() == CanalEntry.EntryType.TRANSACTIONEND) {
                return events;
            }

            if (entry.getEntryType() != CanalEntry.EntryType.ROWDATA) {
                return events;
            }

            CanalEntry.RowChange rowChange = CanalEntry.RowChange.parseFrom(entry.getStoreValue());
            CanalEntry.EventType eventType = rowChange.getEventType();

            if (rowChange.getIsDdl()) {
                handleDdlEvent(entry, rowChange);
                return events;
            }

            if (eventType == CanalEntry.EventType.QUERY) {
                return events;
            }

            String database = entry.getHeader().getSchemaName();
            String table = entry.getHeader().getTableName();
            String fullTableName = database + "." + table;

            String sourceTable = shardConfig.getSourceTable();
            if (sourceTable != null && !sourceTable.equalsIgnoreCase(fullTableName)) {
                return events;
            }

            for (CanalEntry.RowData rowData : rowChange.getRowDatasList()) {
                DataChangeEvent event = buildEvent(entry, rowData, eventType, database, table, receiveTime);
                if (event != null) {
                    events.add(event);
                }
            }
        } catch (Exception e) {
            logger.error("Parse binlog event error for shard: {}", shardConfig.getId(), e);
        }

        return events;
    }

    private void handleDdlEvent(CanalEntry.Entry entry, CanalEntry.RowChange rowChange) {
        String sql = rowChange.getSql();
        CanalEntry.EventType eventType = rowChange.getEventType();

        logger.info("DDL event detected on shard {}: {}, SQL: {}",
                shardConfig.getId(), eventType, sql);

        if (ddlEventListener != null) {
            ddlEventListener.onDdlEvent(shardConfig.getId(), eventType, sql);
        }
    }

    private DataChangeEvent buildEvent(CanalEntry.Entry entry, CanalEntry.RowData rowData,
                                       CanalEntry.EventType eventType, String database, String table,
                                       long receiveTime) {
        Map<String, String> beforeColumns = new HashMap<>();
        Map<String, String> afterColumns = new HashMap<>();
        List<String> primaryKeys = new ArrayList<>();

        for (CanalEntry.Column column : rowData.getBeforeColumnsList()) {
            beforeColumns.put(column.getName(), column.getValue());
            if (column.getIsKey()) {
                primaryKeys.add(column.getName());
            }
        }

        for (CanalEntry.Column column : rowData.getAfterColumnsList()) {
            afterColumns.put(column.getName(), column.getValue());
            if (column.getIsKey() && !primaryKeys.contains(column.getName())) {
                primaryKeys.add(column.getName());
            }
        }

        Map<String, String> columns = eventType == CanalEntry.EventType.DELETE ? beforeColumns : afterColumns;

        String globalIdColumn = shardConfig.getGlobalIdColumn();
        String shardKeyColumn = shardConfig.getShardKey();

        String globalId = columns.get(globalIdColumn);
        String shardKey = columns.get(shardKeyColumn);

        if (globalId == null || globalId.isEmpty()) {
            logger.warn("Global id column {} not found in row data for shard: {}",
                    globalIdColumn, shardConfig.getId());
            return null;
        }

        return DataChangeEvent.builder()
                .shardId(shardConfig.getId())
                .database(database)
                .table(table)
                .eventType(eventType)
                .beforeColumns(beforeColumns)
                .afterColumns(afterColumns)
                .primaryKeys(primaryKeys)
                .globalId(globalId)
                .shardKey(shardKey)
                .executeTime(entry.getHeader().getExecuteTime())
                .receiveTime(receiveTime)
                .binlogJournal(entry.getHeader().getLogfileName())
                .binlogPosition(entry.getHeader().getLogfileOffset())
                .build();
    }
}
