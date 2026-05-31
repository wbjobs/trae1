package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class ZookeeperConfig {
    private String servers;
    private String namespace;
    private int connectionTimeoutMs = 15000;
    private int sessionTimeoutMs = 60000;
    private int retryBaseSleepMs = 1000;
    private int retryMaxRetries = 3;
}
