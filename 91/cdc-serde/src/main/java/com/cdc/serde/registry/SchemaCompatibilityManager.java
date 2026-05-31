package com.cdc.serde.registry;

import com.cdc.common.config.SchemaRegistryConfig;
import io.confluent.kafka.schemaregistry.client.CachedSchemaRegistryClient;
import io.confluent.kafka.schemaregistry.client.SchemaRegistryClient;
import io.confluent.kafka.schemaregistry.client.rest.entities.Config;
import io.confluent.kafka.schemaregistry.client.rest.exceptions.RestClientException;
import org.apache.avro.Schema;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

public class SchemaCompatibilityManager {

    private static final Logger logger = LoggerFactory.getLogger(SchemaCompatibilityManager.class);

    private final SchemaRegistryClient client;
    private final SchemaRegistryConfig config;

    public SchemaCompatibilityManager(SchemaRegistryConfig config) {
        this.config = config;
        this.client = new CachedSchemaRegistryClient(config.getUrl(), 1000);
    }

    public void setGlobalCompatibility() throws IOException, RestClientException {
        Config configObj = new Config();
        configObj.setCompatibilityLevel(mapCompatibilityLevel(config.getCompatibilityLevel()));
        client.updateConfig(null, configObj);
        logger.info("Set global schema compatibility level to: {}", config.getCompatibilityLevel());
    }

    public void setSubjectCompatibility(String subject) throws IOException, RestClientException {
        Config configObj = new Config();
        configObj.setCompatibilityLevel(mapCompatibilityLevel(config.getCompatibilityLevel()));
        client.updateConfig(subject, configObj);
        logger.info("Set subject {} compatibility level to: {}", subject, config.getCompatibilityLevel());
    }

    public boolean isCompatible(String subject, Schema newSchema) throws IOException, RestClientException {
        try {
            client.testCompatibility(subject, newSchema);
            logger.debug("Schema is compatible with subject {}", subject);
            return true;
        } catch (RestClientException e) {
            if (e.getStatus() == 409) {
                logger.warn("Schema is NOT compatible with subject {}: {}", subject, e.getMessage());
                return false;
            }
            throw e;
        }
    }

    public int registerSchema(String subject, Schema schema) throws IOException, RestClientException {
        if (config.isAutoRegister()) {
            int id = client.register(subject, schema);
            logger.info("Registered schema for subject {} with id {}", subject, id);
            return id;
        } else {
            int id = client.getId(subject, schema);
            logger.info("Schema already registered for subject {} with id {}", subject, id);
            return id;
        }
    }

    public Schema getLatestSchema(String subject) throws IOException, RestClientException {
        try {
            return client.getLatestSchemaMetadata(subject).getSchema();
        } catch (RestClientException e) {
            if (e.getStatus() == 404) {
                return null;
            }
            throw e;
        }
    }

    private String mapCompatibilityLevel(SchemaRegistryConfig.CompatibilityLevel level) {
        switch (level) {
            case BACKWARD:
                return "BACKWARD";
            case BACKWARD_TRANSITIVE:
                return "BACKWARD_TRANSITIVE";
            case FORWARD:
                return "FORWARD";
            case FORWARD_TRANSITIVE:
                return "FORWARD_TRANSITIVE";
            case FULL:
                return "FULL";
            case FULL_TRANSITIVE:
                return "FULL_TRANSITIVE";
            case NONE:
                return "NONE";
            default:
                return "BACKWARD";
        }
    }
}
