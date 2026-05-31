package com.clickstream.service;

import com.clickstream.config.KafkaStreamsConfig;
import com.clickstream.model.AnomalySession;
import com.clickstream.model.BlacklistEntry;
import com.clickstream.store.BlacklistStore;
import org.apache.kafka.clients.producer.KafkaProducer;
import org.apache.kafka.clients.producer.ProducerConfig;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.common.serialization.StringSerializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.kafka.core.KafkaTemplate;
import org.springframework.stereotype.Service;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.util.Properties;

@Service
public class BlacklistProducerService {

    private static final Logger logger = LoggerFactory.getLogger(BlacklistProducerService.class);

    @Value("${spring.kafka.bootstrap-servers:localhost:9092}")
    private String bootstrapServers;

    @Value("${kafka.blacklist.topic:blacklist-entries}")
    private String blacklistTopic;

    private final KafkaStreamsConfig kafkaStreamsConfig;
    private final BlacklistStore blacklistStore;
    
    private KafkaProducer<String, String> producer;

    @Autowired
    public BlacklistProducerService(KafkaStreamsConfig kafkaStreamsConfig, BlacklistStore blacklistStore) {
        this.kafkaStreamsConfig = kafkaStreamsConfig;
        this.blacklistStore = blacklistStore;
    }

    @PostConstruct
    public void init() {
        Properties props = new Properties();
        props.put(ProducerConfig.BOOTSTRAP_SERVERS_CONFIG, bootstrapServers);
        props.put(ProducerConfig.KEY_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
        props.put(ProducerConfig.VALUE_SERIALIZER_CLASS_CONFIG, StringSerializer.class.getName());
        props.put(ProducerConfig.ACKS_CONFIG, "all");
        props.put(ProducerConfig.RETRIES_CONFIG, 3);
        
        producer = new KafkaProducer<>(props);
        logger.info("Blacklist Kafka producer initialized");
    }

    @PreDestroy
    public void close() {
        if (producer != null) {
            producer.close();
            logger.info("Blacklist Kafka producer closed");
        }
    }

    public void publishBlacklistEntry(BlacklistEntry entry) {
        try {
            String key = entry.getUserId() != null ? entry.getUserId() : entry.getIpAddress();
            String value = kafkaStreamsConfig.objectMapper().writeValueAsString(entry);
            
            ProducerRecord<String, String> record = new ProducerRecord<>(blacklistTopic, key, value);
            producer.send(record, (metadata, exception) -> {
                if (exception != null) {
                    logger.error("Failed to send blacklist entry for key={}: {}", key, exception.getMessage());
                } else {
                    logger.info("Successfully published blacklist entry: key={}, topic={}, partition={}, offset={}",
                            key, metadata.topic(), metadata.partition(), metadata.offset());
                }
            });
            
            blacklistStore.addToBlacklist(entry);
            
        } catch (Exception e) {
            logger.error("Error publishing blacklist entry", e);
        }
    }

    public void publishBlacklistFromAnomaly(AnomalySession anomaly) {
        BlacklistEntry entry = BlacklistEntry.fromAnomaly(anomaly);
        publishBlacklistEntry(entry);
    }
}
