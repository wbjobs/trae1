package com.alibaba.polardb.index.rebuild;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.alibaba.polardb.index.model.BinlogPosition;
import com.alibaba.polardb.index.zookeeper.ZkPositionManager;
import com.zaxxer.hikari.HikariConfig;
import com.zaxxer.hikari.HikariDataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Service;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicLong;

@Service
public class FullRebuildService {

    private static final Logger logger = LoggerFactory.getLogger(FullRebuildService.class);

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    private GlobalIndexDao globalIndexDao;

    @Autowired
    private ZkPositionManager zkPositionManager;

    private final ConcurrentHashMap<String, JdbcTemplate> shardJdbcTemplates = new ConcurrentHashMap<>();
    private volatile boolean rebuildInProgress = false;

    public synchronized boolean rebuildAll() {
        if (rebuildInProgress) {
            logger.warn("Rebuild already in progress");
            return false;
        }

        if (zkPositionManager.isRebuildInProgress()) {
            logger.warn("Rebuild marker exists in ZooKeeper, another rebuild may be in progress");
            return false;
        }

        rebuildInProgress = true;

        try {
            zkPositionManager.markRebuildStart();
            logger.info("Starting full rebuild of global index...");

            globalIndexDao.truncateIndexTable();
            zkPositionManager.clearAllPositions();

            List<ShardConfig> shards = properties.getShards();
            CountDownLatch latch = new CountDownLatch(shards.size());
            AtomicLong totalCount = new AtomicLong(0);

            for (ShardConfig shard : shards) {
                new Thread(() -> {
                    try {
                        long count = rebuildShard(shard);
                        totalCount.addAndGet(count);
                        logger.info("Shard {} rebuild completed, {} records", shard.getId(), count);
                    } catch (Exception e) {
                        logger.error("Rebuild failed for shard {}", shard.getId(), e);
                    } finally {
                        latch.countDown();
                    }
                }, "rebuild-" + shard.getId()).start();
            }

            latch.await();

            long totalIndexCount = globalIndexDao.count();
            logger.info("Full rebuild completed. Total records processed: {}, index count: {}",
                    totalCount.get(), totalIndexCount);

            zkPositionManager.markRebuildComplete();
            return true;

        } catch (Exception e) {
            logger.error("Full rebuild failed", e);
            zkPositionManager.markRebuildComplete();
            return false;
        } finally {
            rebuildInProgress = false;
        }
    }

    public long rebuildShard(ShardConfig shardConfig) throws Exception {
        String shardId = shardConfig.getId();
        logger.info("Starting rebuild for shard: {}", shardId);

        JdbcTemplate jdbcTemplate = getShardJdbcTemplate(shardConfig);

        String sourceTable = shardConfig.getSourceTable();
        String shardKeyColumn = shardConfig.getShardKey();
        String globalIdColumn = shardConfig.getGlobalIdColumn();
        int batchSize = shardConfig.getBatchSize();

        String countSql = String.format("SELECT COUNT(*) FROM %s", sourceTable);
        Long totalCount = jdbcTemplate.queryForObject(countSql, Long.class);
        long count = totalCount != null ? totalCount : 0;
        logger.info("Shard {} has {} total records", shardId, count);

        if (count == 0) {
            markShardRebuildComplete(shardConfig);
            return 0;
        }

        String selectSql = String.format(
                "SELECT %s, %s FROM %s ORDER BY %s LIMIT ?, ?",
                globalIdColumn, shardKeyColumn, sourceTable, globalIdColumn
        );

        long processedCount = 0;
        int offset = 0;
        Date now = new Date();

        while (offset < count) {
            try {
                List<GlobalIndex> batch = jdbcTemplate.query(
                        selectSql,
                        new Object[]{offset, batchSize},
                        new RowMapper<GlobalIndex>() {
                            @Override
                            public GlobalIndex mapRow(ResultSet rs, int rowNum) throws SQLException {
                                return GlobalIndex.builder()
                                        .globalId(rs.getString(globalIdColumn))
                                        .shardKey(rs.getString(shardKeyColumn))
                                        .shardId(shardId)
                                        .gmtCreate(now)
                                        .gmtModified(now)
                                        .build();
                            }
                        }
                );

                if (batch.isEmpty()) {
                    break;
                }

                globalIndexDao.batchUpsert(batch);
                processedCount += batch.size();
                offset += batchSize;

                if (processedCount % 10000 == 0) {
                    logger.info("Shard {} rebuild progress: {}/{}", shardId, processedCount, count);
                }

            } catch (Exception e) {
                logger.error("Error processing batch at offset {} for shard {}", offset, shardId, e);
                throw e;
            }
        }

        markShardRebuildComplete(shardConfig);
        logger.info("Shard {} rebuild finished, processed {} records", shardId, processedCount);
        return processedCount;
    }

    private void markShardRebuildComplete(ShardConfig shardConfig) {
        BinlogPosition position = BinlogPosition.builder()
                .journalName("REBUILD_COMPLETE")
                .position(System.currentTimeMillis())
                .timestamp(System.currentTimeMillis())
                .serverId(shardConfig.getId())
                .build();
        zkPositionManager.savePosition(shardConfig.getId(), position);
    }

    public synchronized boolean rebuildShard(String shardId) {
        if (rebuildInProgress) {
            logger.warn("Rebuild already in progress");
            return false;
        }

        ShardConfig shardConfig = findShardConfig(shardId);
        if (shardConfig == null) {
            logger.error("Shard config not found: {}", shardId);
            return false;
        }

        rebuildInProgress = true;
        try {
            globalIndexDao.deleteByShardId(shardId);
            zkPositionManager.clearPosition(shardId);

            long count = rebuildShard(shardConfig);
            logger.info("Shard {} rebuild completed, {} records", shardId, count);
            return true;
        } catch (Exception e) {
            logger.error("Rebuild failed for shard {}", shardId, e);
            return false;
        } finally {
            rebuildInProgress = false;
        }
    }

    private JdbcTemplate getShardJdbcTemplate(ShardConfig shardConfig) {
        return shardJdbcTemplates.computeIfAbsent(shardConfig.getId(), k -> {
            HikariConfig config = new HikariConfig();
            config.setJdbcUrl(shardConfig.getDbUrl());
            config.setUsername(shardConfig.getDbUsername());
            config.setPassword(shardConfig.getDbPassword());
            config.setDriverClassName("com.mysql.cj.jdbc.Driver");
            config.setMaximumPoolSize(8);
            config.setMinimumIdle(2);
            config.setConnectionTimeout(3000);
            config.setAutoCommit(true);
            config.addDataSourceProperty("rewriteBatchedStatements", "true");
            HikariDataSource dataSource = new HikariDataSource(config);
            return new JdbcTemplate(dataSource);
        });
    }

    private ShardConfig findShardConfig(String shardId) {
        for (ShardConfig config : properties.getShards()) {
            if (config.getId().equals(shardId)) {
                return config;
            }
        }
        return null;
    }

    public boolean isRebuildInProgress() {
        return rebuildInProgress || zkPositionManager.isRebuildInProgress();
    }
}
