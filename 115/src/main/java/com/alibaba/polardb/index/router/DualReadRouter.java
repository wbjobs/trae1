package com.alibaba.polardb.index.router;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.RebalanceConfig;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.alibaba.polardb.index.topology.ShardTopologyListener;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import java.util.*;
import java.util.concurrent.*;
import java.util.stream.Collectors;

@Component
public class DualReadRouter {

    private static final Logger logger = LoggerFactory.getLogger(DualReadRouter.class);

    @Autowired
    private ShardTopologyListener topologyListener;

    @Autowired
    private GlobalIndexDao globalIndexDao;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired(required = false)
    @Qualifier("centerJdbcTemplate")
    private JdbcTemplate centerJdbcTemplate;

    private ExecutorService dualReadExecutor;
    private final ConcurrentHashMap<String, JdbcTemplate> shardJdbcTemplates = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        RebalanceConfig config = properties.getRebalance();
        int threads = config != null ? Math.max(config.getThreads(), 4) : 8;
        dualReadExecutor = new ThreadPoolExecutor(
                threads, threads * 2, 60L, TimeUnit.SECONDS,
                new LinkedBlockingQueue<>(10000),
                r -> {
                    Thread t = new Thread(r, "dual-read-" + r.hashCode());
                    t.setDaemon(true);
                    return t;
                },
                new ThreadPoolExecutor.CallerRunsPolicy()
        );
        initializeShardConnections();
    }

    private void initializeShardConnections() {
        properties.getShards().forEach(shard -> {
            try {
                if (shard.getDbUrl() != null && !shard.getDbUrl().isEmpty()) {
                    JdbcTemplate jdbcTemplate = createShardJdbcTemplate(shard);
                    shardJdbcTemplates.put(shard.getId(), jdbcTemplate);
                }
            } catch (Exception e) {
                logger.warn("Failed to create JDBC template for shard: {}", shard.getId(), e);
            }
        });
    }

    private JdbcTemplate createShardJdbcTemplate(com.alibaba.polardb.index.config.ShardConfig shard) {
        com.zaxxer.hikari.HikariConfig config = new com.zaxxer.hikari.HikariConfig();
        config.setJdbcUrl(shard.getDbUrl());
        config.setUsername(shard.getDbUsername());
        config.setPassword(shard.getDbPassword());
        config.setDriverClassName("com.mysql.cj.jdbc.Driver");
        config.setMaximumPoolSize(4);
        config.setMinimumIdle(1);
        config.setConnectionTimeout(3000);
        config.setAutoCommit(true);
        com.zaxxer.hikari.HikariDataSource dataSource = new com.zaxxer.hikari.HikariDataSource(config);
        return new JdbcTemplate(dataSource);
    }

    public GlobalIndex queryWithDualRead(String globalId) {
        if (!topologyListener.isDualReadMode()) {
            return globalIndexDao.findByGlobalId(globalId);
        }

        RebalanceConfig config = properties.getRebalance();
        long timeout = config != null ? config.getDualReadTimeoutMs() : 3000;

        String expectedShardId = topologyListener.getShardIdByGlobalId(globalId);
        GlobalIndex indexFromTable = globalIndexDao.findByGlobalId(globalId);

        if (expectedShardId == null) {
            return indexFromTable;
        }

        String currentShardId = indexFromTable != null ? indexFromTable.getShardId() : null;

        if (expectedShardId.equals(currentShardId)) {
            return indexFromTable;
        }

        List<String> shardsToCheck = new ArrayList<>();
        if (currentShardId != null) {
            shardsToCheck.add(currentShardId);
        }
        if (!expectedShardId.equals(currentShardId)) {
            shardsToCheck.add(expectedShardId);
        }

        Set<String> checkedShards = new HashSet<>();
        for (String shardId : shardsToCheck) {
            if (checkedShards.add(shardId)) {
                Future<Boolean> future = dualReadExecutor.submit(() -> checkDataInShard(globalId, shardId));
                try {
                    Boolean exists = future.get(timeout, TimeUnit.MILLISECONDS);
                    if (Boolean.TRUE.equals(exists)) {
                        GlobalIndex result = GlobalIndex.builder()
                                .globalId(globalId)
                                .shardId(shardId)
                                .shardKey(indexFromTable != null ? indexFromTable.getShardKey() : null)
                                .gmtCreate(new Date())
                                .gmtModified(new Date())
                                .build();

                        if (!shardId.equals(currentShardId)) {
                            updateIndexAsync(globalId, shardId);
                        }

                        return result;
                    }
                } catch (TimeoutException e) {
                    logger.warn("Dual read timeout for shard: {}, globalId: {}", shardId, globalId);
                    future.cancel(true);
                } catch (Exception e) {
                    logger.warn("Dual read error for shard: {}, globalId: {}", shardId, globalId, e);
                }
            }
        }

        return indexFromTable;
    }

    public List<GlobalIndex> batchQueryWithDualRead(List<String> globalIds) {
        if (!topologyListener.isDualReadMode() || globalIds == null || globalIds.isEmpty()) {
            return globalIds.stream()
                    .map(globalIndexDao::findByGlobalId)
                    .filter(Objects::nonNull)
                    .collect(Collectors.toList());
        }

        List<Future<GlobalIndex>> futures = new ArrayList<>();
        for (String globalId : globalIds) {
            futures.add(dualReadExecutor.submit(() -> queryWithDualRead(globalId)));
        }

        List<GlobalIndex> results = new ArrayList<>();
        RebalanceConfig config = properties.getRebalance();
        long timeout = config != null ? config.getDualReadTimeoutMs() : 3000;

        for (int i = 0; i < futures.size(); i++) {
            try {
                GlobalIndex index = futures.get(i).get(timeout, TimeUnit.MILLISECONDS);
                if (index != null) {
                    results.add(index);
                }
            } catch (Exception e) {
                logger.warn("Batch dual read error for globalId: {}", globalIds.get(i), e);
            }
        }

        return results;
    }

    private boolean checkDataInShard(String globalId, String shardId) {
        JdbcTemplate jdbcTemplate = shardJdbcTemplates.get(shardId);
        if (jdbcTemplate == null) {
            com.alibaba.polardb.index.config.ShardConfig shardConfig = topologyListener.getShardConfig(shardId);
            if (shardConfig != null && shardConfig.getDbUrl() != null) {
                try {
                    jdbcTemplate = createShardJdbcTemplate(shardConfig);
                    shardJdbcTemplates.put(shardId, jdbcTemplate);
                } catch (Exception e) {
                    logger.warn("Failed to create JDBC template for shard: {}", shardId, e);
                    return false;
                }
            } else {
                return false;
            }
        }

        com.alibaba.polardb.index.config.ShardConfig shardConfig = topologyListener.getShardConfig(shardId);
        if (shardConfig == null || shardConfig.getSourceTable() == null) {
            return false;
        }

        String sourceTable = shardConfig.getSourceTable();
        String globalIdColumn = shardConfig.getGlobalIdColumn();

        try {
            String sql = String.format(
                    "SELECT COUNT(*) FROM %s WHERE %s = ? LIMIT 1",
                    sourceTable, globalIdColumn
            );
            Long count = jdbcTemplate.queryForObject(sql, Long.class, globalId);
            return count != null && count > 0;
        } catch (Exception e) {
            logger.debug("Data not found in shard {} for globalId {}: {}", shardId, globalId, e.getMessage());
            return false;
        }
    }

    private void updateIndexAsync(String globalId, String newShardId) {
        dualReadExecutor.submit(() -> {
            try {
                Map<String, String> update = new HashMap<>();
                update.put("globalId", globalId);
                update.put("newShardId", newShardId);
                globalIndexDao.batchUpdateShardId(Collections.singletonList(update));
                logger.info("Auto-updated index for globalId: {} -> shard: {}", globalId, newShardId);
            } catch (Exception e) {
                logger.warn("Failed to auto-update index for globalId: {}", globalId, e);
            }
        });
    }

    public void clearShardConnections() {
        shardJdbcTemplates.forEach((shardId, jdbcTemplate) -> {
            try {
                javax.sql.DataSource dataSource = jdbcTemplate.getDataSource();
                if (dataSource instanceof com.zaxxer.hikari.HikariDataSource) {
                    ((com.zaxxer.hikari.HikariDataSource) dataSource).close();
                }
            } catch (Exception e) {
                logger.warn("Error closing connection for shard: {}", shardId, e);
            }
        });
        shardJdbcTemplates.clear();
    }

    public void refreshShardConnections() {
        clearShardConnections();
        initializeShardConnections();
    }

    public ExecutorService getDualReadExecutor() {
        return dualReadExecutor;
    }

    public void shutdown() {
        clearShardConnections();
        if (dualReadExecutor != null) {
            dualReadExecutor.shutdown();
            try {
                if (!dualReadExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                    dualReadExecutor.shutdownNow();
                }
            } catch (InterruptedException e) {
                dualReadExecutor.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }
        logger.info("DualReadRouter shutdown");
    }
}
