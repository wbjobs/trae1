package com.cdc.core.kafka;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.config.KafkaConfig;
import com.cdc.common.config.SchemaRegistryConfig;
import com.cdc.common.event.CdcEvent;
import com.cdc.common.event.TableSchema;
import com.cdc.common.util.TableNameUtil;
import com.cdc.serde.avro.CdcAvroSerializer;
import io.confluent.kafka.serializers.KafkaAvroSerializerConfig;
import io.prometheus.client.Histogram;
import org.apache.kafka.clients.producer.KafkaProducer;
import org.apache.kafka.clients.producer.Producer;
import org.apache.kafka.clients.producer.ProducerConfig;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.clients.producer.RecordMetadata;
import org.apache.kafka.common.serialization.StringSerializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.Map;
import java.util.Properties;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

public class KafkaProducerManager {

    private static final Logger logger = LoggerFactory.getLogger(KafkaProducerManager.class);

    private final CdcConfig config;
    private Producer<String, CdcEvent> producer;
    private CdcAvroSerializer serializer;

    public KafkaProducerManager(CdcConfig config) {
        this.config = config;
    }

    public void start() {
        Properties props = buildProducerConfig();
        serializer = new CdcAvroSerializer();

        Map<String, Object> serdeConfigs = new HashMap<>();
        serdeConfigs.put(KafkaAvroSerializerConfig.SCHEMA_REGISTRY_URL_CONFIG,
                config.getSchemaRegistry().getUrl());
        serdeConfigs.put("auto.register.schemas", String.valueOf(config.getSchemaRegistry().isAutoRegister()));
        serdeConfigs.put(KafkaAvroSerializerConfig.VALUE_SUBJECT_NAME_STRATEGY,
                "io.confluent.kafka.serializers.subject.TopicNameStrategy");

        if (config.getSchemaRegistry().getProperties() != null) {
            serdeConfigs.putAll(config.getSchemaRegistry().getProperties());
        }

        serializer.configure(serdeConfigs, false);

        props.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
        props.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG, serializer.getClass().getName());

        producer = new KafkaProducer<>(props, new StringSerializer(), serializer);
        logger.info("Kafka producer started with brokers: {}", config.getKafka().getBootstrapServers());
    }

    public void registerTableSchema(TableSchema tableSchema) {
        if (serializer != null) {
            serializer.registerTableSchema(tableSchema);
        }
    }

    public Future<RecordMetadata> send(CdcEvent event, Histogram.Timer timer) {
        String topic = TableNameUtil.getTopicName(event.getFullTableName());
        String key = buildMessageKey(event);

        ProducerRecord<String, CdcEvent> record = new ProducerRecord<>(topic, key, event);

        return producer.send(record, (metadata, exception) -> {
            if (timer != null) {
                timer.observeDuration();
            }
            if (exception != null) {
                logger.error("Failed to send message to topic {}: {}", topic, exception.getMessage(), exception);
            } else {
                logger.debug("Sent message to topic {} partition {} offset {}",
                        topic, metadata.partition(), metadata.offset());
            }
        });
    }

    public RecordMetadata sendSync(CdcEvent event, long timeout, TimeUnit unit)
            throws InterruptedException, ExecutionException, TimeoutException {
        String topic = TableNameUtil.getTopicName(event.getFullTableName());
        String key = buildMessageKey(event);

        ProducerRecord<String, CdcEvent> record = new ProducerRecord<>(topic, key, event);
        return producer.send(record).get(timeout, unit);
    }

    private String buildMessageKey(CdcEvent event) {
        Map<String, Object> data = event.getAfter() != null ? event.getAfter() : event.getBefore();
        if (data == null) {
            return event.getFullTableName();
        }

        StringBuilder keyBuilder = new StringBuilder();
        keyBuilder.append(event.getSchemaName()).append(".").append(event.getTableName());

        for (Map.Entry<String, Object> entry : data.entrySet()) {
            if (entry.getKey().toLowerCase().contains("id") || entry.getKey().equalsIgnoreCase("id")) {
                keyBuilder.append("|").append(entry.getKey()).append("=").append(entry.getValue());
                break;
            }
        }

        return keyBuilder.toString();
    }

    public void flush() {
        if (producer != null) {
            producer.flush();
        }
    }

    public void stop() {
        if (producer != null) {
            producer.flush();
            producer.close(10, TimeUnit.SECONDS);
            logger.info("Kafka producer stopped");
        }
        if (serializer != null) {
            serializer.close();
        }
    }

    private Properties buildProducerConfig() {
        KafkaConfig kafkaConfig = config.getKafka();
        Properties props = new Properties();

        props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, kafkaConfig.getBootstrapServers());
        props.put(ProducerConfig.ACKS_CONFIG, kafkaConfig.getAcks());
        props.put(ProducerConfig.RETRIES_CONFIG, kafkaConfig.getRetries());
        props.put(ProducerConfig.BATCH_SIZE_CONFIG, kafkaConfig.getBatchSize());
        props.put(ProducerConfig.LINGER_MS_CONFIG, kafkaConfig.getLingerMs());
        props.put(ProducerConfig.BUFFER_MEMORY_CONFIG, kafkaConfig.getBufferMemory());
        props.put(ProducerConfig.COMPRESSION_TYPE_CONFIG, kafkaConfig.getCompressionType());
        props.put(ProducerConfig.ENABLE_IDEMPOTENCE_CONFIG, true);
        props.put(ProducerConfig.MAX_IN_FLIGHT_REQUESTS_PER_CONNECTION, 5);
        props.put(ProducerConfig.DELIVERY_TIMEOUT_MS_CONFIG, 120000);
        props.put(ProducerConfig.REQUEST_TIMEOUT_MS_CONFIG, 30000);

        if (kafkaConfig.getProperties() != null) {
            props.putAll(kafkaConfig.getProperties());
        }

        return props;
    }

    public int getQueueSize() {
        if (producer != null) {
            return producer.metrics().values().stream()
                    .filter(m -> m.metricName().name().equals("record-queue-time-avg"))
                    .findFirst()
                    .map(m -> m.value().intValue())
                    .orElse(0);
        }
        return 0;
    }
}
