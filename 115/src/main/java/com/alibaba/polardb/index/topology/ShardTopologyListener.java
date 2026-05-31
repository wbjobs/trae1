package com.alibaba.polardb.index.topology;

import com.alibaba.otter.canal.protocol.CanalEntry;
import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.RebalanceConfig;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.hash.ConsistentHash;
import com.alibaba.polardb.index.monitor.SyncMetrics;
import com.alibaba.polardb.index.rebalance.IndexRebalanceService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Lazy;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

@Component
public class ShardTopologyListener {

    private static final Logger logger = LoggerFactory.getLogger(ShardTopologyListener.class);

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    @Lazy
    private IndexRebalanceService rebalanceService;

    @Autowired(required = false)
    private JdbcTemplate centerJdbcTemplate;

    @Autowired(required = false)
    private SyncMetrics syncMetrics;

    private final AtomicReference<Set<String>> currentShardIds = new AtomicReference<>(new HashSet<>());
    private final AtomicReference<ConsistentHash<String>> consistentHash = new AtomicReference<>();
    private final AtomicReference<Map<String, ShardConfig>> shardConfigMap = new AtomicReference<>(new ConcurrentHashMap<>());
    private final AtomicBoolean rebalanceInProgress = new AtomicBoolean(false);
    private final AtomicBoolean dualReadMode = new AtomicBoolean(false);

    private volatile long lastTopologyCheckTime = 0;
    private volatile long topologyVersion = 0;

    @PostConstruct
    public void init() {
        initializeTopology();
        logger.info("ShardTopologyListener initialized with {} shards", currentShardIds.get().size());
    }

    public synchronized void initializeTopology() {
        List<ShardConfig> shards = properties.getShards();
        if (shards == null || shards.isEmpty()) {
            logger.warn("No shard configs found");
            return;
        }

        Set<String> shardIds = new HashSet<>();
        Map<String, ShardConfig> configMap = new ConcurrentHashMap<>();
        List<String> hashNodes = new ArrayList<>();

        for (ShardConfig shard : shards) {
            if (shard.getStatus() == 1) {
                shardIds.add(shard.getId());
                configMap.put(shard.getId(), shard);
                for (int i = 0; i < shard.getHashWeight(); i++) {
                    hashNodes.add(shard.getHashKey());
                }
            }
        }

        currentShardIds.set(shardIds);
        shardConfigMap.set(configMap);

        RebalanceConfig rebalanceConfig = properties.getRebalance();
        int virtualNodes = rebalanceConfig != null ? rebalanceConfig.getVirtualNodes() : 160;
        consistentHash.set(new ConsistentHash<>(virtualNodes, hashNodes));

        topologyVersion++;
        if (syncMetrics != null) {
            syncMetrics.setTopologyVersion(topologyVersion);
        }
        logger.info("Topology initialized, version: {}, shards: {}", topologyVersion, shardIds);
    }

    @Scheduled(fixedDelayString = "${global-index.sync.rebalance.check-interval-ms:300000}")
    public void checkTopologyChange() {
        RebalanceConfig config = properties.getRebalance();
        if (config == null || !config.isEnabled()) {
            return;
        }

        if (rebalanceInProgress.get()) {
            logger.debug("Rebalance in progress, skip topology check");
            return;
        }

        try {
            lastTopologyCheckTime = System.currentTimeMillis();
            logger.debug("Checking shard topology...");

            Map<String, ShardConfig> detectedShards = detectShardTopology();
            Set<String> detectedShardIds = detectedShards.keySet();
            Set<String> currentIds = currentShardIds.get();

            if (!detectedShardIds.equals(currentIds)) {
                logger.info("Topology change detected! Current: {}, Detected: {}", currentIds, detectedShardIds);
                handleTopologyChange(detectedShards);
            } else {
                logger.debug("No topology change detected");
            }
        } catch (Exception e) {
            logger.error("Error checking topology change", e);
        }
    }

    private Map<String, ShardConfig> detectShardTopology() {
        Map<String, ShardConfig> result = new LinkedHashMap<>();
        List<ShardConfig> configuredShards = properties.getShards();

        if (configuredShards != null) {
            for (ShardConfig shard : configuredShards) {
                if (shard.getStatus() == 1) {
                    result.put(shard.getId(), shard);
                }
            }
        }

        RebalanceConfig config = properties.getRebalance();
        if (config != null && config.getMetadataTable() != null && centerJdbcTemplate != null) {
            try {
                result.putAll(queryShardMetadata(config.getMetadataTable()));
            } catch (Exception e) {
                logger.warn("Failed to query shard metadata from table: {}", config.getMetadataTable(), e);
            }
        }

        if (config != null && config.getTopologyCheckSql() != null && centerJdbcTemplate != null) {
            try {
                result.putAll(executeTopologyCheckSql(config.getTopologyCheckSql()));
            } catch (Exception e) {
                logger.warn("Failed to execute topology check SQL", e);
            }
        }

        return result;
    }

    private Map<String, ShardConfig> queryShardMetadata(String metadataTable) {
        Map<String, ShardConfig> result = new LinkedHashMap<>();
        try {
            String sql = "SELECT shard_id, shard_name, status, db_url, hash_weight FROM " + metadataTable
                    + " WHERE status = 1";
            List<Map<String, Object>> rows = centerJdbcTemplate.queryForList(sql);
            for (Map<String, Object> row : rows) {
                String shardId = String.valueOf(row.get("shard_id"));
                ShardConfig shard = new ShardConfig();
                shard.setId(shardId);
                shard.setName(row.get("shard_name") != null ? String.valueOf(row.get("shard_name")) : shardId);
                shard.setStatus(row.get("status") != null ? ((Number) row.get("status")).intValue() : 1);
                shard.setDbUrl(row.get("db_url") != null ? String.valueOf(row.get("db_url")) : null);
                shard.setHashWeight(row.get("hash_weight") != null ? ((Number) row.get("hash_weight")).intValue() : 100);
                result.put(shardId, shard);
            }
        } catch (Exception e) {
            logger.error("Error querying shard metadata", e);
        }
        return result;
    }

    private Map<String, ShardConfig> executeTopologyCheckSql(String sql) {
        Map<String, ShardConfig> result = new LinkedHashMap<>();
        try {
            List<Map<String, Object>> rows = centerJdbcTemplate.queryForList(sql);
            for (Map<String, Object> row : rows) {
                String shardId = String.valueOf(row.get("shard_id"));
                ShardConfig shard = new ShardConfig();
                shard.setId(shardId);
                shard.setName(row.get("shard_name") != null ? String.valueOf(row.get("shard_name")) : shardId);
                shard.setStatus(row.get("status") != null ? ((Number) row.get("status")).intValue() : 1);
                if (row.get("hash_weight") != null) {
                    shard.setHashWeight(((Number) row.get("hash_weight")).intValue());
                }
                result.put(shardId, shard);
            }
        } catch (Exception e) {
            logger.error("Error executing topology check SQL", e);
        }
        return result;
    }

    public void onDdlEvent(String shardId, CanalEntry.EventType eventType, String sql) {
        if (sql == null) {
            return;
        }

        String lowerSql = sql.toLowerCase();
        if (lowerSql.contains("create table") || lowerSql.contains("alter table")
                || lowerSql.contains("drop table") || lowerSql.contains("truncate table")) {
            logger.info("DDL event detected on shard {}: {}", shardId, sql);

            if (isShardSplitOrMergeDdl(sql)) {
                logger.warn("Shard split/merge DDL detected, triggering topology check");
                checkTopologyChange();
            }
        }
    }

    private boolean isShardSplitOrMergeDdl(String sql) {
        String lowerSql = sql.toLowerCase();
        return lowerSql.contains("split") || lowerSql.contains("merge")
                || lowerSql.contains("add partition") || lowerSql.contains("drop partition")
                || lowerSql.contains("split partition") || lowerSql.contains("merge partition");
    }

    private synchronized void handleTopologyChange(Map<String, ShardConfig> newShardConfigs) {
        if (rebalanceInProgress.compareAndSet(false, true)) {
            try {
                Set<String> oldShardIds = currentShardIds.get();
                Set<String> newShardIds = newShardConfigs.keySet();

                Set<String> addedShards = new HashSet<>(newShardIds);
                addedShards.removeAll(oldShardIds);

                Set<String> removedShards = new HashSet<>(oldShardIds);
                removedShards.removeAll(newShardIds);

                Set<String> commonShards = new HashSet<>(oldShardIds);
                commonShards.retainAll(newShardIds);

                logger.info("Topology change details - Added: {}, Removed: {}, Common: {}",
                        addedShards, removedShards, commonShards);

                enableDualReadMode();

                topologyVersion++;
                currentShardIds.set(newShardIds);
                shardConfigMap.set(new ConcurrentHashMap<>(newShardConfigs));

                rebuildConsistentHash(newShardConfigs);

                triggerRebalance(addedShards, removedShards);

            } finally {
                rebalanceInProgress.set(false);
            }
        }
    }

    private void rebuildConsistentHash(Map<String, ShardConfig> shardConfigs) {
        RebalanceConfig rebalanceConfig = properties.getRebalance();
        int virtualNodes = rebalanceConfig != null ? rebalanceConfig.getVirtualNodes() : 160;

        List<String> hashNodes = new ArrayList<>();
        for (ShardConfig shard : shardConfigs.values()) {
            if (shard.getStatus() == 1) {
                for (int i = 0; i < shard.getHashWeight(); i++) {
                    hashNodes.add(shard.getHashKey());
                }
            }
        }

        consistentHash.set(new ConsistentHash<>(virtualNodes, hashNodes));
        logger.info("Consistent hash rebuilt with {} virtual nodes, {} hash entries",
                virtualNodes, hashNodes.size());
    }

    private void enableDualReadMode() {
        RebalanceConfig config = properties.getRebalance();
        if (config != null && config.isEnableDualRead()) {
            dualReadMode.set(true);
            if (syncMetrics != null) {
                syncMetrics.setDualReadModeActive(true);
            }
            logger.info("Dual read mode enabled for rebalance");
        }
    }

    public void disableDualReadMode() {
        dualReadMode.set(false);
        if (syncMetrics != null) {
            syncMetrics.setDualReadModeActive(false);
        }
        logger.info("Dual read mode disabled");
    }

    private void triggerRebalance(Set<String> addedShards, Set<String> removedShards) {
        logger.info("Triggering rebalance for added shards: {}, removed shards: {}", addedShards, removedShards);
        rebalanceService.startRebalance(addedShards, removedShards, topologyVersion);
    }

    public String getShardIdByGlobalId(String globalId) {
        ConsistentHash<String> hash = consistentHash.get();
        if (hash == null) {
            return null;
        }
        return hash.get(globalId);
    }

    public boolean isDualReadMode() {
        return dualReadMode.get();
    }

    public boolean isRebalanceInProgress() {
        return rebalanceInProgress.get();
    }

    public Set<String> getCurrentShardIds() {
        return new HashSet<>(currentShardIds.get());
    }

    public ShardConfig getShardConfig(String shardId) {
        return shardConfigMap.get().get(shardId);
    }

    public Map<String, ShardConfig> getAllShardConfigs() {
        return new HashMap<>(shardConfigMap.get());
    }

    public long getTopologyVersion() {
        return topologyVersion;
    }

    public ConsistentHash<String> getConsistentHash() {
        return consistentHash.get();
    }

    public long getLastTopologyCheckTime() {
        return lastTopologyCheckTime;
    }

    @PreDestroy
    public void destroy() {
        dualReadMode.set(false);
        rebalanceInProgress.set(false);
        logger.info("ShardTopologyListener destroyed");
    }
}
