package com.clickstream.config;

import com.clickstream.model.*;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import org.apache.kafka.common.serialization.Deserializer;
import org.apache.kafka.common.serialization.Serde;
import org.apache.kafka.common.serialization.Serdes;
import org.apache.kafka.common.serialization.Serializer;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.util.HashMap;
import java.util.Map;

@Configuration
public class KafkaStreamsConfig {

    @Bean
    public ObjectMapper objectMapper() {
        ObjectMapper mapper = new ObjectMapper();
        mapper.registerModule(new JavaTimeModule());
        mapper.disable(SerializationFeature.WRITE_DATES_AS_TIMESTAMPS);
        return mapper;
    }

    @Bean
    public Serde<ClickEvent> clickEventSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(ClickEvent.class, objectMapper);
    }

    @Bean
    public Serde<SessionAggregate> sessionAggregateSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(SessionAggregate.class, objectMapper);
    }

    @Bean
    public Serde<Session> sessionSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(Session.class, objectMapper);
    }

    @Bean
    public Serde<SessionDetail> sessionDetailSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(SessionDetail.class, objectMapper);
    }

    @Bean
    public Serde<AnomalySession> anomalySessionSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(AnomalySession.class, objectMapper);
    }

    @Bean
    public Serde<BlacklistEntry> blacklistEntrySerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(BlacklistEntry.class, objectMapper);
    }

    @Bean
    public Serde<AnomalyDetectionProcessor.IpSessionCount> ipSessionCountSerde(ObjectMapper objectMapper) {
        return new JsonSerde<>(AnomalyDetectionProcessor.IpSessionCount.class, objectMapper);
    }

    public static class JsonSerde<T> implements Serde<T> {

        private final Class<T> type;
        private final ObjectMapper objectMapper;

        public JsonSerde(Class<T> type, ObjectMapper objectMapper) {
            this.type = type;
            this.objectMapper = objectMapper;
        }

        @Override
        public Serializer<T> serializer() {
            return new JsonSerializer<>(objectMapper);
        }

        @Override
        public Deserializer<T> deserializer() {
            return new JsonDeserializer<>(type, objectMapper);
        }

        @Override
        public void configure(Map<String, ?> configs, boolean isKey) {
        }

        @Override
        public void close() {
        }
    }

    public static class JsonSerializer<T> implements Serializer<T> {

        private final ObjectMapper objectMapper;

        public JsonSerializer(ObjectMapper objectMapper) {
            this.objectMapper = objectMapper;
        }

        @Override
        public byte[] serialize(String topic, T data) {
            if (data == null) {
                return null;
            }
            try {
                return objectMapper.writeValueAsBytes(data);
            } catch (Exception e) {
                throw new RuntimeException("Error serializing JSON", e);
            }
        }

        @Override
        public void configure(Map<String, ?> configs, boolean isKey) {
        }

        @Override
        public void close() {
        }
    }

    public static class JsonDeserializer<T> implements Deserializer<T> {

        private final Class<T> type;
        private final ObjectMapper objectMapper;

        public JsonDeserializer(Class<T> type, ObjectMapper objectMapper) {
            this.type = type;
            this.objectMapper = objectMapper;
        }

        @Override
        public T deserialize(String topic, byte[] data) {
            if (data == null) {
                return null;
            }
            try {
                return objectMapper.readValue(data, type);
            } catch (Exception e) {
                throw new RuntimeException("Error deserializing JSON", e);
            }
        }

        @Override
        public void configure(Map<String, ?> configs, boolean isKey) {
        }

        @Override
        public void close() {
        }
    }
}
