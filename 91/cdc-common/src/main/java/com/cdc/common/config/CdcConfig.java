package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.ArrayList;
import java.util.List;

public class CdcConfig {

    @JsonProperty("name")
    private String name = "cdc-sync";

    @JsonProperty("database")
    private DatabaseConfig database;

    @JsonProperty("kafka")
    private KafkaConfig kafka;

    @JsonProperty("schemaRegistry")
    private SchemaRegistryConfig schemaRegistry;

    @JsonProperty("tableFilters")
    private List<TableFilterConfig> tableFilters = new ArrayList<>();

    @JsonProperty("snapshotMode")
    private SnapshotMode snapshotMode = SnapshotMode.INITIAL;

    @JsonProperty("metrics")
    private MetricsConfig metrics;

    @JsonProperty("ddl")
    private DdlConfig ddl = new DdlConfig();

    @JsonProperty("dataMasking")
    private DataMaskingConfig dataMasking = new DataMaskingConfig();

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public DatabaseConfig getDatabase() {
        return database;
    }

    public void setDatabase(DatabaseConfig database) {
        this.database = database;
    }

    public KafkaConfig getKafka() {
        return kafka;
    }

    public void setKafka(KafkaConfig kafka) {
        this.kafka = kafka;
    }

    public SchemaRegistryConfig getSchemaRegistry() {
        return schemaRegistry;
    }

    public void setSchemaRegistry(SchemaRegistryConfig schemaRegistry) {
        this.schemaRegistry = schemaRegistry;
    }

    public List<TableFilterConfig> getTableFilters() {
        return tableFilters;
    }

    public void setTableFilters(List<TableFilterConfig> tableFilters) {
        this.tableFilters = tableFilters;
    }

    public SnapshotMode getSnapshotMode() {
        return snapshotMode;
    }

    public void setSnapshotMode(SnapshotMode snapshotMode) {
        this.snapshotMode = snapshotMode;
    }

    public MetricsConfig getMetrics() {
        return metrics;
    }

    public void setMetrics(MetricsConfig metrics) {
        this.metrics = metrics;
    }

    public DdlConfig getDdl() {
        return ddl;
    }

    public void setDdl(DdlConfig ddl) {
        this.ddl = ddl;
    }

    public DataMaskingConfig getDataMasking() {
        return dataMasking;
    }

    public void setDataMasking(DataMaskingConfig dataMasking) {
        this.dataMasking = dataMasking;
    }

    public enum SnapshotMode {
        @JsonProperty("initial")
        INITIAL,
        @JsonProperty("initial_only")
        INITIAL_ONLY,
        @JsonProperty("never")
        NEVER,
        @JsonProperty("schema_only")
        SCHEMA_ONLY,
        @JsonProperty("schema_only_recovery")
        SCHEMA_ONLY_RECOVERY
    }
}
