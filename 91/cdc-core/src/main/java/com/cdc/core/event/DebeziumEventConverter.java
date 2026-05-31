package com.cdc.core.event;

import com.cdc.common.config.TableFilterConfig;
import com.cdc.common.event.CdcEvent;
import com.cdc.common.event.DdlEvent;
import com.cdc.common.event.TableSchema;
import com.cdc.common.util.TableNameUtil;
import com.cdc.core.ddl.DdlEventParser;
import io.debezium.data.Envelope;
import io.debezium.data.SchemaChange;
import org.apache.kafka.connect.data.Field;
import org.apache.kafka.connect.data.Struct;
import org.apache.kafka.connect.source.SourceRecord;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Instant;
import java.util.HashMap;
import java.util.Map;

public class DebeziumEventConverter {

    private static final Logger logger = LoggerFactory.getLogger(DebeziumEventConverter.class);

    public static CdcEvent convert(SourceRecord sourceRecord) {
        Struct value = (Struct) sourceRecord.value();
        if (value == null) {
            return null;
        }

        if (value.schema().name().equals(SchemaChange.SCHEMA_NAME)) {
            return null;
        }

        Envelope.Operation operation = Envelope.Operation.forCode(value.getString("op"));
        if (operation == null) {
            logger.warn("Unknown operation type in record");
            return null;
        }

        CdcEvent event = new CdcEvent();

        Struct source = value.getStruct("source");
        if (source != null) {
            event.setSourceDatabase(source.getString("db"));
            event.setSchemaName(source.getString("schema"));
            event.setTableName(source.getString("table"));
            event.setSource(buildSourceMap(source));
            Long tsMs = source.getInt64("ts_ms");
            if (tsMs != null) {
                event.setTimestamp(Instant.ofEpochMilli(tsMs));
            }
        }

        event.setOperation(mapOperation(operation));

        if (value.getStruct("before") != null) {
            event.setBefore(extractRowData(value.getStruct("before"), event.getFullTableName()));
        }
        if (value.getStruct("after") != null) {
            event.setAfter(extractRowData(value.getStruct("after"), event.getFullTableName()));
        }

        if (value.get("ts_ms") instanceof Long) {
            event.setTimestamp(Instant.ofEpochMilli(value.getInt64("ts_ms")));
        }

        if (value.get("transaction") != null) {
            Struct transaction = value.getStruct("transaction");
            if (transaction != null) {
                event.setTransactionId(transaction.getString("id"));
            }
        }

        return event;
    }

    public static DdlEvent convertDdl(SourceRecord sourceRecord, String dbType) {
        Struct value = (Struct) sourceRecord.value();
        if (value == null) {
            return null;
        }

        if (!value.schema().name().equals(SchemaChange.SCHEMA_NAME)) {
            return null;
        }

        String database = null;
        String schema = null;
        String ddl = null;

        if (value.get("databaseName") != null) {
            database = value.getString("databaseName");
        }

        if (value.get("schemaName") != null) {
            schema = value.getString("schemaName");
        }

        if (value.get("ddl") != null) {
            ddl = value.getString("ddl");
        }

        if (ddl == null || ddl.isEmpty()) {
            logger.debug("Empty DDL in schema change event");
            return null;
        }

        String tableName = extractTableNameFromDdl(ddl, schema);
        if (tableName == null) {
            logger.debug("Could not extract table name from DDL: {}", ddl);
            return null;
        }

        logger.info("Schema change detected for table {}.{}: {}", schema, tableName, ddl);

        return DdlEventParser.parse(database, schema, tableName, ddl, dbType);
    }

    private static String extractTableNameFromDdl(String ddl, String schema) {
        String upperDdl = ddl.toUpperCase();
        String pattern;

        if (upperDdl.contains("ALTER TABLE")) {
            pattern = "(?i)ALTER\\s+TABLE\\s+(?:IF\\s+EXISTS\\s+)?(?:`|\")?(\\w+)(?:`|\")?(?:\\.(?:`|\")?(\\w+)(?:`|\"))?";
        } else if (upperDdl.contains("CREATE TABLE")) {
            pattern = "(?i)CREATE\\s+TABLE\\s+(?:IF\\s+NOT\\s+EXISTS\\s+)?(?:`|\")?(\\w+)(?:`|\"))?(?:\\.(?:`|\")?(\\w+)(?:`|\"))?";
        } else if (upperDdl.contains("DROP TABLE")) {
            pattern = "(?i)DROP\\s+TABLE\\s+(?:IF\\s+EXISTS\\s+)?(?:`|\")?(\\w+)(?:`|\"))?(?:\\.(?:`|\")?(\\w+)(?:`|\"))?";
        } else {
            return null;
        }

        java.util.regex.Matcher matcher = java.util.regex.Pattern.compile(pattern).matcher(ddl);
        if (matcher.find()) {
            if (matcher.groupCount() >= 2 && matcher.group(2) != null) {
                return matcher.group(2);
            }
            return matcher.group(1);
        }
        return null;
    }

    public static boolean isSchemaChangeEvent(SourceRecord sourceRecord) {
        Struct value = (Struct) sourceRecord.value();
        return value != null && value.schema().name().equals(SchemaChange.SCHEMA_NAME);
    }

    public static TableSchema extractTableSchema(SourceRecord sourceRecord) {
        Struct value = (Struct) sourceRecord.value();
        if (value == null) {
            return null;
        }

        Struct source = value.getStruct("source");
        if (source == null) {
            return null;
        }

        TableSchema tableSchema = new TableSchema();
        tableSchema.setSchemaName(source.getString("schema"));
        tableSchema.setTableName(source.getString("table"));

        Struct after = value.getStruct("after");
        if (after != null) {
            for (Field field : after.schema().fields()) {
                TableSchema.ColumnSchema column = new TableSchema.ColumnSchema();
                column.setName(field.name());
                column.setType(field.schema().type().getName());
                column.setNullable(field.schema().isOptional());
                column.setPosition(field.index());
                tableSchema.getColumns().add(column);
            }
        }

        return tableSchema;
    }

    private static Map<String, Object> buildSourceMap(Struct source) {
        Map<String, Object> sourceMap = new HashMap<>();
        for (Field field : source.schema().fields()) {
            sourceMap.put(field.name(), source.get(field));
        }
        return sourceMap;
    }

    private static Map<String, Object> extractRowData(Struct struct, String fullTableName) {
        if (struct == null) {
            return null;
        }

        TableFilterConfig filterConfig = TableNameUtil.getTableFilter(fullTableName);
        Map<String, Object> data = new HashMap<>();

        for (Field field : struct.schema().fields()) {
            if (filterConfig != null && !filterConfig.isColumnIncluded(field.name())) {
                continue;
            }
            Object value = struct.get(field);
            data.put(field.name(), value);
        }

        return data;
    }

    private static CdcEvent.OperationType mapOperation(Envelope.Operation operation) {
        switch (operation) {
            case CREATE:
            case READ:
                return CdcEvent.OperationType.INSERT;
            case UPDATE:
                return CdcEvent.OperationType.UPDATE;
            case DELETE:
                return CdcEvent.OperationType.DELETE;
            default:
                throw new IllegalArgumentException("Unknown operation: " + operation);
        }
    }
}
