package com.cdc.core.connector;

import com.cdc.common.config.CdcConfig;
import com.cdc.common.config.DatabaseConfig;
import com.cdc.common.exception.CdcException;
import io.debezium.connector.mysql.MySqlConnector;
import io.debezium.connector.mysql.MySqlConnectorConfig;
import io.debezium.connector.postgresql.PostgresConnector;
import io.debezium.connector.postgresql.PostgresConnectorConfig;
import io.debezium.embedded.EmbeddedEngine;
import io.debezium.relational.history.FileDatabaseHistory;
import io.debezium.relational.history.KafkaDatabaseHistory;
import org.apache.kafka.connect.storage.FileOffsetBackingStore;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.util.Properties;

public class DebeziumConnectorFactory {

    private static final Logger logger = LoggerFactory.getLogger(DebeziumConnectorFactory.class);

    public static EmbeddedEngine createEngine(CdcConfig config, EmbeddedEngine.ChangeConsumer changeConsumer) {
        Properties props = buildDebeziumConfig(config);
        logger.info("Creating Debezium engine with config: {}", props);

        return EmbeddedEngine.create()
                .using(props)
                .notifying(changeConsumer)
                .build();
    }

    private static Properties buildDebeziumConfig(CdcConfig config) {
        DatabaseConfig dbConfig = config.getDatabase();
        Properties props = new Properties();

        props.setProperty("name", config.getName());
        props.setProperty("database.server.id", String.valueOf(dbConfig.getServerId()));
        props.setProperty("database.server.name", dbConfig.getServerName());
        props.setProperty("database.hostname", dbConfig.getHostname());
        props.setProperty("database.port", String.valueOf(dbConfig.getPort()));
        props.setProperty("database.user", dbConfig.getUsername());
        props.setProperty("database.password", dbConfig.getPassword());
        props.setProperty("database.dbname", dbConfig.getDatabase());
        props.setProperty("database.history", FileDatabaseHistory.class.getName());
        props.setProperty("database.history.file.filename", "./data/db-history.dat");
        props.setProperty("offset.storage", FileOffsetBackingStore.class.getName());
        props.setProperty("offset.storage.file.filename", "./data/offsets.dat");
        props.setProperty("offset.flush.interval.ms", "60000");
        props.setProperty("snapshot.mode", mapSnapshotMode(config.getSnapshotMode()));
        props.setProperty("include.schema.changes", "true");
        props.setProperty("decimal.handling.mode", "string");
        props.setProperty("time.precision.mode", "connect");

        if (config.getTableFilters() != null && !config.getTableFilters().isEmpty()) {
            StringBuilder tableIncludeList = new StringBuilder();
            for (int i = 0; i < config.getTableFilters().size(); i++) {
                if (i > 0) {
                    tableIncludeList.append(",");
                }
                tableIncludeList.append(config.getTableFilters().get(i).getTableName());
            }
            props.setProperty("table.include.list", tableIncludeList.toString());
            logger.info("Filtering tables: {}", tableIncludeList);
        }

        switch (dbConfig.getType()) {
            case MYSQL:
                configureMySql(props, dbConfig);
                break;
            case POSTGRESQL:
                configurePostgreSql(props, dbConfig);
                break;
            default:
                throw new CdcException("Unsupported database type: " + dbConfig.getType());
        }

        return props;
    }

    private static void configureMySql(Properties props, DatabaseConfig dbConfig) {
        props.setProperty("connector.class", MySqlConnector.class.getName());
        props.setProperty("database.include.list", dbConfig.getDatabase());
        props.setProperty("database.serverTimezone", "UTC");
        props.setProperty("snapshot.locking.mode", "minimal");
        props.setProperty("binlog.buffer.size", "0");
        logger.info("Configured MySQL connector for database: {}", dbConfig.getDatabase());
    }

    private static void configurePostgreSql(Properties props, DatabaseConfig dbConfig) {
        props.setProperty("connector.class", PostgresConnector.class.getName());
        props.setProperty("database.dbname", dbConfig.getDatabase());
        props.setProperty("plugin.name", "pgoutput");
        props.setProperty("slot.name", "cdc_slot_" + dbConfig.getDatabase().replaceAll("[^a-z0-9]", "_"));
        props.setProperty("publication.name", "cdc_publication");
        props.setProperty("publication.autocreate.mode", "all_tables");
        logger.info("Configured PostgreSQL connector for database: {}", dbConfig.getDatabase());
    }

    private static String mapSnapshotMode(CdcConfig.SnapshotMode mode) {
        switch (mode) {
            case INITIAL:
                return "initial";
            case INITIAL_ONLY:
                return "initial_only";
            case NEVER:
                return "never";
            case SCHEMA_ONLY:
                return "schema_only";
            case SCHEMA_ONLY_RECOVERY:
                return "schema_only_recovery";
            default:
                return "initial";
        }
    }

    public static void ensureDataDirectory() {
        File dataDir = new File("./data");
        if (!dataDir.exists()) {
            if (dataDir.mkdirs()) {
                logger.info("Created data directory: {}", dataDir.getAbsolutePath());
            } else {
                logger.warn("Failed to create data directory: {}", dataDir.getAbsolutePath());
            }
        }
    }
}
