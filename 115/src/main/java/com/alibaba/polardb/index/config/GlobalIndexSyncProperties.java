package com.alibaba.polardb.index.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

import java.util.List;

@Data
@Configuration
@ConfigurationProperties(prefix = "global-index.sync")
public class GlobalIndexSyncProperties {
    private String destination;
    private ZookeeperConfig zookeeper;
    private CenterDbConfig centerDb;
    private List<ShardConfig> shards;
    private IndexTableConfig indexTable;
    private ProcessorConfig processor;
    private RebalanceConfig rebalance;
    private JoinConfig join;
}
