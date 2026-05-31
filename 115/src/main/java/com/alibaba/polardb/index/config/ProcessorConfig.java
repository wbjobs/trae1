package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class ProcessorConfig {
    private int threadPoolSize = 32;
    private int batchQueueCapacity = 10000;
    private long flushIntervalMs = 100;
    private int maxBatchSize = 512;
}
