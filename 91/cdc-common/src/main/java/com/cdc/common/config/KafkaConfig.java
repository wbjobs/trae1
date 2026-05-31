package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.HashMap;
import java.util.Map;

public class KafkaConfig {

    @JsonProperty("bootstrapServers")
    private String bootstrapServers = "localhost:9092";

    @JsonProperty("topicPrefix")
    private String topicPrefix = "";

    @JsonProperty("acks")
    private String acks = "all";

    @JsonProperty("retries")
    private int retries = 3;

    @JsonProperty("batchSize")
    private int batchSize = 16384;

    @JsonProperty("lingerMs")
    private long lingerMs = 0;

    @JsonProperty("bufferMemory")
    private long bufferMemory = 33554432;

    @JsonProperty("compressionType")
    private String compressionType = "none";

    @JsonProperty("properties")
    private Map<String, String> properties = new HashMap<>();

    public String getBootstrapServers() {
        return bootstrapServers;
    }

    public void setBootstrapServers(String bootstrapServers) {
        this.bootstrapServers = bootstrapServers;
    }

    public String getTopicPrefix() {
        return topicPrefix;
    }

    public void setTopicPrefix(String topicPrefix) {
        this.topicPrefix = topicPrefix;
    }

    public String getAcks() {
        return acks;
    }

    public void setAcks(String acks) {
        this.acks = acks;
    }

    public int getRetries() {
        return retries;
    }

    public void setRetries(int retries) {
        this.retries = retries;
    }

    public int getBatchSize() {
        return batchSize;
    }

    public void setBatchSize(int batchSize) {
        this.batchSize = batchSize;
    }

    public long getLingerMs() {
        return lingerMs;
    }

    public void setLingerMs(long lingerMs) {
        this.lingerMs = lingerMs;
    }

    public long getBufferMemory() {
        return bufferMemory;
    }

    public void setBufferMemory(long bufferMemory) {
        this.bufferMemory = bufferMemory;
    }

    public String getCompressionType() {
        return compressionType;
    }

    public void setCompressionType(String compressionType) {
        this.compressionType = compressionType;
    }

    public Map<String, String> getProperties() {
        return properties;
    }

    public void setProperties(Map<String, String> properties) {
        this.properties = properties;
    }
}
