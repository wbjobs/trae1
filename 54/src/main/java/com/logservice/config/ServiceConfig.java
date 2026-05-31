package com.logservice.config;

public class ServiceConfig {
    private int httpPort = 8080;
    private String dataDir = "./data/logs";
    private int blockSizeBytes = 1024 * 1024;
    private long blockFlushIntervalMs = 5000L;
    private int zstdCompressionLevel = 3;
    private long retentionDays = 7L;
    private long retentionCheckIntervalMs = 60_000L;
    private int searchContextLines = 3;
    private int searchTimeoutMs = 5000;
    private int decompressCacheSize = 64;
    private int httpBossThreads = 1;
    private int httpWorkerThreads = 0;

    private String aggregateLevelRegex = "\\b(ERROR|WARN|WARNING|INFO|DEBUG|TRACE|FATAL|CRIT)\\b";
    private String aggregateModuleRegex = "\\[([a-zA-Z0-9_\\-]+)\\]";
    private String aggregateWordRegex = "[a-zA-Z0-9_\\-]+";
    private long aggregationRollIntervalMs = 60_000L;
    private long aggregationRetentionMs = 24L * 60 * 60 * 1000;
    private int aggregationTimeoutMs = 3000;

    public int getHttpPort() { return httpPort; }
    public void setHttpPort(int httpPort) { this.httpPort = httpPort; }

    public String getDataDir() { return dataDir; }
    public void setDataDir(String dataDir) { this.dataDir = dataDir; }

    public int getBlockSizeBytes() { return blockSizeBytes; }
    public void setBlockSizeBytes(int blockSizeBytes) { this.blockSizeBytes = blockSizeBytes; }

    public long getBlockFlushIntervalMs() { return blockFlushIntervalMs; }
    public void setBlockFlushIntervalMs(long blockFlushIntervalMs) { this.blockFlushIntervalMs = blockFlushIntervalMs; }

    public int getZstdCompressionLevel() { return zstdCompressionLevel; }
    public void setZstdCompressionLevel(int zstdCompressionLevel) { this.zstdCompressionLevel = zstdCompressionLevel; }

    public long getRetentionDays() { return retentionDays; }
    public void setRetentionDays(long retentionDays) { this.retentionDays = retentionDays; }

    public long getRetentionCheckIntervalMs() { return retentionCheckIntervalMs; }
    public void setRetentionCheckIntervalMs(long retentionCheckIntervalMs) { this.retentionCheckIntervalMs = retentionCheckIntervalMs; }

    public int getSearchContextLines() { return searchContextLines; }
    public void setSearchContextLines(int searchContextLines) { this.searchContextLines = searchContextLines; }

    public int getSearchTimeoutMs() { return searchTimeoutMs; }
    public void setSearchTimeoutMs(int searchTimeoutMs) { this.searchTimeoutMs = searchTimeoutMs; }

    public int getDecompressCacheSize() { return decompressCacheSize; }
    public void setDecompressCacheSize(int decompressCacheSize) { this.decompressCacheSize = decompressCacheSize; }

    public int getHttpBossThreads() { return httpBossThreads; }
    public void setHttpBossThreads(int httpBossThreads) { this.httpBossThreads = httpBossThreads; }

    public int getHttpWorkerThreads() { return httpWorkerThreads; }
    public void setHttpWorkerThreads(int httpWorkerThreads) { this.httpWorkerThreads = httpWorkerThreads; }

    public String getAggregateLevelRegex() { return aggregateLevelRegex; }
    public void setAggregateLevelRegex(String aggregateLevelRegex) { this.aggregateLevelRegex = aggregateLevelRegex; }

    public String getAggregateModuleRegex() { return aggregateModuleRegex; }
    public void setAggregateModuleRegex(String aggregateModuleRegex) { this.aggregateModuleRegex = aggregateModuleRegex; }

    public String getAggregateWordRegex() { return aggregateWordRegex; }
    public void setAggregateWordRegex(String aggregateWordRegex) { this.aggregateWordRegex = aggregateWordRegex; }

    public long getAggregationRollIntervalMs() { return aggregationRollIntervalMs; }
    public void setAggregationRollIntervalMs(long aggregationRollIntervalMs) { this.aggregationRollIntervalMs = aggregationRollIntervalMs; }

    public long getAggregationRetentionMs() { return aggregationRetentionMs; }
    public void setAggregationRetentionMs(long aggregationRetentionMs) { this.aggregationRetentionMs = aggregationRetentionMs; }

    public int getAggregationTimeoutMs() { return aggregationTimeoutMs; }
    public void setAggregationTimeoutMs(int aggregationTimeoutMs) { this.aggregationTimeoutMs = aggregationTimeoutMs; }
}
