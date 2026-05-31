package com.cdc.core;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.util.TableNameUtil;
import com.cdc.core.alert.DingTalkAlertService;
import com.cdc.core.connector.DebeziumConnectorFactory;
import com.cdc.core.ddl.DdlPolicyEngine;
import com.cdc.core.event.CdcChangeConsumer;
import com.cdc.core.kafka.KafkaProducerManager;
import com.cdc.core.masking.MaskingRuleEngine;
import com.cdc.core.masking.RuleRepositoryManager;
import com.cdc.core.status.TableSyncStatusManager;
import com.cdc.metrics.MetricsManager;
import com.cdc.serde.registry.SchemaCompatibilityManager;
import io.debezium.embedded.EmbeddedEngine;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class CdcEngine {

    private static final Logger logger = LoggerFactory.getLogger(CdcEngine.class);

    private final CdcConfig config;
    private EmbeddedEngine engine;
    private KafkaProducerManager kafkaProducer;
    private MetricsManager metricsManager;
    private SchemaCompatibilityManager schemaCompatibilityManager;
    private TableSyncStatusManager statusManager;
    private DdlPolicyEngine ddlPolicyEngine;
    private DingTalkAlertService alertService;
    private MaskingRuleEngine maskingRuleEngine;
    private RuleRepositoryManager ruleRepositoryManager;
    private CdcChangeConsumer changeConsumer;
    private ExecutorService executor;
    private volatile boolean running = false;

    public CdcEngine(CdcConfig config) {
        this.config = config;
    }

    public void start() throws Exception {
        logger.info("Starting CDC engine...");

        validateConfig(config);

        TableNameUtil.setTopicPrefix(config.getKafka().getTopicPrefix());
        TableNameUtil.registerTableFilters(config.getTableFilters());

        DebeziumConnectorFactory.ensureDataDirectory();

        if (config.getMetrics() != null && config.getMetrics().isEnabled()) {
            metricsManager = new MetricsManager(config.getMetrics());
            metricsManager.start();
        }

        if (config.getSchemaRegistry() != null) {
            schemaCompatibilityManager = new SchemaCompatibilityManager(config.getSchemaRegistry());
            try {
                schemaCompatibilityManager.setGlobalCompatibility();
            } catch (Exception e) {
                logger.warn("Could not set global schema compatibility: {}", e.getMessage());
            }
        }

        statusManager = new TableSyncStatusManager(config.getDdl().getStateStorePath());

        alertService = new DingTalkAlertService(config.getDdl());

        ddlPolicyEngine = new DdlPolicyEngine(config, statusManager, alertService);

        if (config.getDataMasking() != null && config.getDataMasking().isEnabled()) {
            maskingRuleEngine = new MaskingRuleEngine(config.getDataMasking(), alertService);
            if (config.getDataMasking().getRuleRepository() != null) {
                ruleRepositoryManager = new RuleRepositoryManager(config.getDataMasking(), maskingRuleEngine);
                ruleRepositoryManager.initialize();
            }
        }

        kafkaProducer = new KafkaProducerManager(config);
        kafkaProducer.start();

        changeConsumer = new CdcChangeConsumer(config, kafkaProducer, metricsManager,
                statusManager, ddlPolicyEngine, alertService, maskingRuleEngine);

        engine = DebeziumConnectorFactory.createEngine(config, changeConsumer);

        executor = Executors.newSingleThreadExecutor(r -> {
            Thread thread = new Thread(r, "cdc-engine");
            thread.setDaemon(false);
            return thread;
        });

        executor.submit(() -> {
            try {
                running = true;
                engine.run();
            } catch (Exception e) {
                logger.error("CDC engine failed", e);
                throw new RuntimeException(e);
            } finally {
                running = false;
            }
        });

        logger.info("CDC engine started successfully");
        logger.info("DDL Policy: {}", config.getDdl().getPolicy());
        if (config.getDdl().getAlert() != null && config.getDdl().getAlert().isEnabled()) {
            logger.info("DingTalk alerts enabled");
        }
        runtimeShutdownHook();
    }

    private void validateConfig(CdcConfig config) {
        if (config.getDatabase() == null) {
            throw new IllegalArgumentException("Database configuration is required");
        }
        if (config.getKafka() == null) {
            throw new IllegalArgumentException("Kafka configuration is required");
        }
        if (config.getSchemaRegistry() == null) {
            throw new IllegalArgumentException("Schema Registry configuration is required");
        }

        logger.info("Configuration validation passed");
        logger.info("  Database type: {}", config.getDatabase().getType());
        logger.info("  Database host: {}:{}", config.getDatabase().getHostname(), config.getDatabase().getPort());
        logger.info("  Kafka brokers: {}", config.getKafka().getBootstrapServers());
        logger.info("  Schema Registry: {}", config.getSchemaRegistry().getUrl());
        logger.info("  Snapshot mode: {}", config.getSnapshotMode());
        logger.info("  DDL policy: {}", config.getDdl().getPolicy());
    }

    private void runtimeShutdownHook() {
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            logger.info("JVM shutdown hook triggered");
            try {
                stop();
            } catch (Exception e) {
                logger.error("Error during shutdown", e);
            }
        }, "cdc-shutdown-hook"));
    }

    public void stop() throws Exception {
        if (!running) {
            return;
        }

        logger.info("Stopping CDC engine...");
        running = false;

        if (changeConsumer != null) {
            changeConsumer.stop();
        }

        if (engine != null) {
            try {
                engine.close();
            } catch (IOException e) {
                logger.warn("Error closing Debezium engine", e);
            }
        }

        if (executor != null) {
            executor.shutdown();
            try {
                if (!executor.awaitTermination(30, TimeUnit.SECONDS)) {
                    executor.shutdownNow();
                }
            } catch (InterruptedException e) {
                executor.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }

        if (kafkaProducer != null) {
            kafkaProducer.stop();
        }

        if (metricsManager != null) {
            metricsManager.stop();
        }

        if (statusManager != null) {
            statusManager.saveState();
        }

        if (maskingRuleEngine != null) {
            maskingRuleEngine.shutdown();
        }

        if (ruleRepositoryManager != null) {
            ruleRepositoryManager.shutdown();
        }

        logger.info("CDC engine stopped successfully");
    }

    public boolean isRunning() {
        return running;
    }

    public CdcConfig getConfig() {
        return config;
    }

    public KafkaProducerManager getKafkaProducer() {
        return kafkaProducer;
    }

    public MetricsManager getMetricsManager() {
        return metricsManager;
    }

    public TableSyncStatusManager getStatusManager() {
        return statusManager;
    }

    public DdlPolicyEngine getDdlPolicyEngine() {
        return ddlPolicyEngine;
    }

    public MaskingRuleEngine getMaskingRuleEngine() {
        return maskingRuleEngine;
    }

    public RuleRepositoryManager getRuleRepositoryManager() {
        return ruleRepositoryManager;
    }
}
