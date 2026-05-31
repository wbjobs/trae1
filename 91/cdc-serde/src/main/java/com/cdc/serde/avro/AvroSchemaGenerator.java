package com.cdc.serde.avro;

import com.cdc.common.config.TableFilterConfig;
import com.cdc.common.event.TableSchema;
import org.apache.avro.Schema;
import org.apache.avro.SchemaBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;

public class AvroSchemaGenerator {

    private static final Logger logger = LoggerFactory.getLogger(AvroSchemaGenerator.class);

    public static Schema generateEventSchema(TableSchema tableSchema, TableFilterConfig filterConfig) {
        String namespace = "com.cdc.avro";
        String recordName = buildRecordName(tableSchema);

        SchemaBuilder.FieldAssembler<Schema> fieldAssembler = SchemaBuilder.record(recordName)
                .namespace(namespace)
                .fields();

        fieldAssembler.name("operation")
                .type().enumeration("OperationType")
                .namespace(namespace)
                .symbols("INSERT", "UPDATE", "DELETE", "READ")
                .noDefault();

        fieldAssembler.name("timestamp")
                .type().optional().longType();

        fieldAssembler.name("transactionId")
                .type().optional().stringType();

        List<TableSchema.ColumnSchema> filteredColumns = filterColumns(tableSchema.getColumns(), filterConfig);

        Schema beforeSchema = buildRowSchema(recordName + "_before", namespace, filteredColumns);
        Schema afterSchema = buildRowSchema(recordName + "_after", namespace, filteredColumns);

        fieldAssembler.name("before")
                .type().optional().type(beforeSchema);

        fieldAssembler.name("after")
                .type().optional().type(afterSchema);

        Schema sourceSchema = buildSourceSchema(namespace);
        fieldAssembler.name("source")
                .type().optional().type(sourceSchema);

        return fieldAssembler.endRecord();
    }

    private static Schema buildRowSchema(String recordName, String namespace, List<TableSchema.ColumnSchema> columns) {
        SchemaBuilder.FieldAssembler<Schema> fieldAssembler = SchemaBuilder.record(recordName)
                .namespace(namespace)
                .fields();

        for (TableSchema.ColumnSchema column : columns) {
            Schema avroType = mapSqlTypeToAvro(column.getType());
            if (column.isNullable()) {
                fieldAssembler.name(column.getName())
                        .type().optional().type(avroType);
            } else {
                fieldAssembler.name(column.getName())
                        .type(avroType)
                        .noDefault();
            }
        }

        return fieldAssembler.endRecord();
    }

    private static Schema buildSourceSchema(String namespace) {
        return SchemaBuilder.record("SourceInfo")
                .namespace(namespace)
                .fields()
                .name("database").type().optional().stringType()
                .name("schema").type().optional().stringType()
                .name("table").type().optional().stringType()
                .name("position").type().optional().stringType()
                .name("query").type().optional().stringType()
                .endRecord();
    }

    private static String buildRecordName(TableSchema tableSchema) {
        StringBuilder sb = new StringBuilder();
        if (tableSchema.getSchemaName() != null && !tableSchema.getSchemaName().isEmpty()) {
            sb.append(capitalize(tableSchema.getSchemaName()));
        }
        sb.append(capitalize(tableSchema.getTableName()));
        sb.append("Event");
        return sb.toString().replaceAll("[^a-zA-Z0-9_]", "_");
    }

    private static List<TableSchema.ColumnSchema> filterColumns(List<TableSchema.ColumnSchema> columns, TableFilterConfig filterConfig) {
        if (filterConfig == null) {
            return columns;
        }

        List<TableSchema.ColumnSchema> result = new ArrayList<>();
        for (TableSchema.ColumnSchema column : columns) {
            if (filterConfig.isColumnIncluded(column.getName())) {
                result.add(column);
            }
        }
        logger.debug("Filtered columns for table {}: {} -> {} columns",
                filterConfig.getTableName(), columns.size(), result.size());
        return result;
    }

    public static Schema mapSqlTypeToAvro(String sqlType) {
        if (sqlType == null) {
            return Schema.create(Schema.Type.STRING);
        }

        String lowerType = sqlType.toLowerCase();

        if (lowerType.contains("int") && !lowerType.contains("bigint")) {
            return Schema.create(Schema.Type.INT);
        } else if (lowerType.contains("bigint") || lowerType.contains("serial")) {
            return Schema.create(Schema.Type.LONG);
        } else if (lowerType.contains("float") || lowerType.contains("real")) {
            return Schema.create(Schema.Type.FLOAT);
        } else if (lowerType.contains("double") || lowerType.contains("numeric") || lowerType.contains("decimal")) {
            return Schema.create(Schema.Type.DOUBLE);
        } else if (lowerType.contains("boolean") || lowerType.contains("bool")) {
            return Schema.create(Schema.Type.BOOLEAN);
        } else if (lowerType.contains("binary") || lowerType.contains("blob") || lowerType.contains("bytea")) {
            return Schema.create(Schema.Type.BYTES);
        } else if (lowerType.contains("timestamp") || lowerType.contains("datetime")) {
            return Schema.create(Schema.Type.LONG);
        } else if (lowerType.contains("date")) {
            return Schema.create(Schema.Type.INT);
        } else if (lowerType.contains("time")) {
            return Schema.create(Schema.Type.LONG);
        } else {
            return Schema.create(Schema.Type.STRING);
        }
    }

    private static String capitalize(String s) {
        if (s == null || s.isEmpty()) {
            return s;
        }
        return s.substring(0, 1).toUpperCase() + s.substring(1);
    }
}
