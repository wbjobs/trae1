package com.clickstream.config;

import com.clickstream.processor.AnomalyDetectionProcessor;
import com.clickstream.processor.ClickStreamSessionProcessor;
import jakarta.annotation.PostConstruct;
import org.apache.kafka.streams.StreamsBuilder;
import org.apache.kafka.streams.StreamsConfig;
import org.apache.kafka.streams.KafkaStreams;
import org.apache.kafka.streams.Topology;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.kafka.annotation.EnableKafka;
import org.springframework.kafka.annotation.EnableKafkaStreams;

import java.util.Properties;

@Configuration
@EnableKafka
@EnableKafkaStreams
public class StreamsConfiguration {

    private static final Logger logger = LoggerFactory.getLogger(StreamsConfiguration.class);

    @Value("${spring.kafka.bootstrap-servers:localhost:9092}")
    private String bootstrapServers;

    @Value("${kafka.application.id:clickstream-sessionization}")
    private String applicationId;

    private final ClickStreamSessionProcessor sessionProcessor;
    private final AnomalyDetectionProcessor anomalyDetectionProcessor;

    @Autowired
    public StreamsConfiguration(ClickStreamSessionProcessor sessionProcessor,
                                AnomalyDetectionProcessor anomalyDetectionProcessor) {
        this.sessionProcessor = sessionProcessor;
        this.anomalyDetectionProcessor = anomalyDetectionProcessor;
    }

    @Bean
    public StreamsBuilder streamsBuilder() {
        StreamsBuilder builder = new StreamsBuilder();
        sessionProcessor.buildTopology(builder);
        anomalyDetectionProcessor.buildAnomalyDetectionTopology(builder);
        return builder;
    }

    @Bean
    public KafkaStreams kafkaStreams(StreamsBuilder streamsBuilder) {
        Properties props = new Properties();
        props.put(StreamsConfig.APPLICATION_ID_CONFIG, applicationId);
        props.put(StreamsConfig.BOOTSTRAP_SERVERS_CONFIG, bootstrapServers);
        props.put(StreamsConfig.DEFAULT_KEY_SERDE_CLASS_CONFIG, org.apache.kafka.common.serialization.Serdes.String().getClass());
        props.put(StreamsConfig.DEFAULT_VALUE_SERDE_CLASS_CONFIG, org.apache.kafka.common.serialization.Serdes.ByteArray().getClass());
        props.put(StreamsConfig.STATE_DIR_CONFIG, "/tmp/kafka-streams");
        props.put(StreamsConfig.COMMIT_INTERVAL_MS_CONFIG, 1000);
        props.put(StreamsConfig.CACHE_MAX_BYTES_BUFFERING_CONFIG, 10 * 1024 * 1024L);
        props.put(StreamsConfig.PROCESSING_GUARANTEE_CONFIG, StreamsConfig.AT_LEAST_ONCE);

        Topology topology = streamsBuilder.build(props);
        logger.info("Kafka Streams topology built successfully");

        KafkaStreams kafkaStreams = new KafkaStreams(topology, props);
        
        kafkaStreams.setStateListener((newState, oldState) -> {
            logger.info("Kafka Streams state changed from {} to {}", oldState, newState);
        });

        kafkaStreams.setUncaughtExceptionHandler((thread, exception) -> {
            logger.error("Kafka Streams uncaught exception on thread {}", thread, exception);
        });

        return kafkaStreams;
    }

    @PostConstruct
    public void startStreams(KafkaStreams kafkaStreams) {
        logger.info("Starting Kafka Streams...");
        kafkaStreams.start();
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            logger.info("Shutting down Kafka Streams...");
            kafkaStreams.close();
        }));
    }
}
