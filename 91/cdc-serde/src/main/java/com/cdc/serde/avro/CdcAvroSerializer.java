package com.cdc.serde.avro;

import com.cdc.common.config.TableFilterConfig;
import com.cdc.common.event.CdcEvent;
import com.cdc.common.event.TableSchema;
import com.cdc.common.util.TableNameUtil;
import io.confluent.kafka.schemaregistry.client.CachedSchemaRegistryClient;
import io.confluent.kafka.schemaregistry.client.SchemaRegistryClient;
import io.confluent.kafka.schemaregistry.client.rest.exceptions.RestClientException;
import io.confluent.kafka.serializers.KafkaAvroSerializer;
import io.confluent.kafka.serializers.KafkaAvroSerializerConfig;
import org.apache.avro.Schema;
import org.apache.avro.generic.GenericData;
import org.apache.avro.generic.GenericRecord;
import org.apache.kafka.common.errors.SerializationException;
import org.apache.kafka.common.header.Headers;
import org.apache.kafka.common.serialization.Serializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.time.Instant;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class CdcAvroSerializer implements Serializer<CdcEvent> {

    private static final Logger logger = LoggerFactory.getLogger(CdcAvroSerializer.class);

    private SchemaRegistryClient schemaRegistryClient;
    private KafkaAvroSerializer avroSerializer;
    private final Map<String, Schema> schemaCache = new ConcurrentHashMap<>();
    private final Map<String, TableSchema> tableSchemaCache = new ConcurrentHashMap<>();
    private String schemaRegistryUrl;
    private boolean autoRegister;

    @Override
    public void configure(Map<String, ?> configs, boolean isKey) {
        schemaRegistryUrl = (String) configs.get(KafkaAvroSerializerConfig.SCHEMA_REGISTRY_URL_CONFIG);
        autoRegister = Boolean.parseBoolean(
                String.valueOf(configs.getOrDefault("auto.register.schemas", "true")));

        Map<String, Object> avroConfigs = new HashMap<>(configs);
        schemaRegistryClient = new CachedSchemaRegistryClient(schemaRegistryUrl, 1000);
        avroSerializer = new KafkaAvroSerializer(schemaRegistryClient);
        avroSerializer.configure(avroConfigs, isKey);
    }

    public void registerTableSchema(TableSchema tableSchema) {
        String fullTableName = tableSchema.getFullTableName();
        tableSchemaCache.put(fullTableName, tableSchema);
        logger.info("Registered table schema for {}", fullTableName);
    }

    @Override
    public byte[] serialize(String topic, CdcEvent event) {
        return serialize(topic, null, event);
    }

    @Override
    public byte[] serialize(String topic, Headers headers, CdcEvent event) {
        if (event == null) {
            return null;
        }

        try {
            String fullTableName = event.getFullTableName();
            Schema schema = getOrCreateSchema(fullTableName);
            GenericRecord record = eventToGenericRecord(event, schema);

            return avroSerializer.serialize(topic, headers, record);
        } catch (Exception e) {
            logger.error("Failed to serialize CDC event for table: {}", event.getFullTableName(), e);
            throw new SerializationException("Failed to serialize CDC event", e);
        }
    }

    private Schema getOrCreateSchema(String fullTableName) throws IOException, RestClientException {
        Schema schema = schemaCache.get(fullTableName);
        if (schema != null) {
            return schema;
        }

        TableSchema tableSchema = tableSchemaCache.get(fullTableName);
        if (tableSchema == null) {
            throw new IllegalStateException("No table schema registered for: " + fullTableName);
        }

        TableFilterConfig filterConfig = TableNameUtil.getTableFilter(fullTableName);
        schema = AvroSchemaGenerator.generateEventSchema(tableSchema, filterConfig);

        String subject = TableNameUtil.getTopicName(fullTableName) + "-value";
        if (autoRegister) {
            int schemaId = schemaRegistryClient.register(subject, schema);
            logger.info("Registered schema for subject {} with id {}", subject, schemaId);
        }

        schemaCache.put(fullTableName, schema);
        return schema;
    }

    private GenericRecord eventToGenericRecord(CdcEvent event, Schema schema) {
        GenericRecord record = new GenericData.Record(schema);

        record.put("operation", event.getOperation().name());
        if (event.getTimestamp() != null) {
            record.put("timestamp", event.getTimestamp().toEpochMilli());
        }
        if (event.getTransactionId() != null) {
            record.put("transactionId", event.getTransactionId());
        }

        Schema sourceSchema = schema.getField("source").schema();
        if (sourceSchema.getType() == Schema.Type.UNION) {
            sourceSchema = sourceSchema.getTypes().stream()
                    .filter(s -> s.getType() == Schema.Type.RECORD)
                    .findFirst()
                    .orElseThrow();
        }
        GenericRecord sourceRecord = buildSourceRecord(event, sourceSchema);
        record.put("source", sourceRecord);

        Schema beforeSchema = schema.getField("before").schema();
        if (beforeSchema.getType() == Schema.Type.UNION) {
            beforeSchema = beforeSchema.getTypes().stream()
                    .filter(s -> s.getType() == Schema.Type.RECORD)
                    .findFirst()
                    .orElse(null);
        }
        if (beforeSchema != null && event.getBefore() != null) {
            GenericRecord beforeRecord = buildRowRecord(event.getBefore(), beforeSchema);
            record.put("before", beforeRecord);
        }

        Schema afterSchema = schema.getField("after").schema();
        if (afterSchema.getType() == Schema.Type.UNION) {
            afterSchema = afterSchema.getTypes().stream()
                    .filter(s -> s.getType() == Schema.Type.RECORD)
                    .findFirst()
                    .orElse(null);
        }
        if (afterSchema != null && event.getAfter() != null) {
            GenericRecord afterRecord = buildRowRecord(event.getAfter(), afterSchema);
            record.put("after", afterRecord);
        }

        return record;
    }

    private GenericRecord buildSourceRecord(CdcEvent event, Schema schema) {
        GenericRecord record = new GenericData.Record(schema);
        record.put("database", event.getSourceDatabase());
        record.put("schema", event.getSchemaName());
        record.put("table", event.getTableName());

        Map<String, Object> source = event.getSource();
        if (source != null) {
            if (source.get("position") != null) {
                record.put("position", source.get("position").toString());
            }
            if (source.get("query") != null) {
                record.put("query", source.get("query").toString());
            }
        }
        return record;
    }

    private GenericRecord buildRowRecord(Map<String, Object> data, Schema schema) {
        GenericRecord record = new GenericData.Record(schema);
        String fullTableName = schema.getName().replace("_before", "").replace("_after", "");
        TableFilterConfig filterConfig = TableNameUtil.getTableFilter(fullTableName);

        for (Schema.Field field : schema.getFields()) {
            if (filterConfig != null && !filterConfig.isColumnIncluded(field.name())) {
                continue;
            }
            Object value = data.get(field.name());
            if (value != null) {
                record.put(field.name(), convertValue(value, field.schema()));
            }
        }
        return record;
    }

    private Object convertValue(Object value, Schema schema) {
        if (value == null) {
            return null;
        }

        if (schema.getType() == Schema.Type.UNION) {
            for (Schema s : schema.getTypes()) {
                if (s.getType() != Schema.Type.NULL) {
                    return convertValue(value, s);
                }
            }
            return value;
        }

        switch (schema.getType()) {
            case INT:
                if (value instanceof Number) {
                    return ((Number) value).intValue();
                } else if (value instanceof java.time.LocalDate) {
                    return (int) ((java.time.LocalDate) value).toEpochDay();
                }
                break;
            case LONG:
                if (value instanceof Number) {
                    return ((Number) value).longValue();
                } else if (value instanceof Instant) {
                    return ((Instant) value).toEpochMilli();
                } else if (value instanceof java.time.LocalDateTime) {
                    return ((java.time.LocalDateTime) value)
                            .atZone(java.time.ZoneId.systemDefault())
                            .toInstant()
                            .toEpochMilli();
                }
                break;
            case FLOAT:
                if (value instanceof Number) {
                    return ((Number) value).floatValue();
                }
                break;
            case DOUBLE:
                if (value instanceof Number) {
                    return ((Number) value).doubleValue();
                }
                break;
            case BOOLEAN:
                if (value instanceof Boolean) {
                    return value;
                }
                break;
            case BYTES:
                if (value instanceof byte[]) {
                    return java.nio.ByteBuffer.wrap((byte[]) value);
                }
                break;
            case STRING:
            default:
                return value.toString();
        }
        return value;
    }

    @Override
    public void close() {
        if (avroSerializer != null) {
            avroSerializer.close();
        }
    }
}
