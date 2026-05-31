package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class RebalanceConfig {
    private boolean enabled = true;
    private int threads = 8;
    private int virtualNodes = 160;
    private long checkIntervalMs = 300000;
    private int batchSize = 500;
    private boolean enableDualRead = true;
    private long dualReadTimeoutMs = 3000;
    private int maxRetryTimes = 3;
    private long retryIntervalMs = 1000;
    private boolean dryRun = false;
    private String metadataTable;
    private String topologyCheckSql;
}
