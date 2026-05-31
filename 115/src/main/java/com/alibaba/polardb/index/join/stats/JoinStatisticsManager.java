package com.alibaba.polardb.index.join.stats;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.join.model.ShardStatistics;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import java.sql.SQLException;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class JoinStatisticsManager {
    private static final Logger logger = LoggerFactory.getLogger(JoinStatisticsManager.class);

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired(required = false)
    private Map<String, JdbcTemplate> shardJdbcTemplates;

    private final Map<String, ShardStatistics> shardStatsMap = new ConcurrentHashMap<>();
    private long lastAnalyzeTime;

    @PostConstruct
    public void init() {
        if (properties.getShards() != null) {
            for (ShardConfig shard : properties.getShards()) {
                shardStatsMap.computeIfAbsent(shard.getShardId(), ShardStatistics::new);
            }
        }
    }

    @Scheduled(fixedDelayString = "${global-index.sync.join.stats.refresh-interval-ms:60000}")
    public void refreshStatistics() {
        if (!isStatisticsEnabled()) {
            return;
        }

        logger.info("Refreshing shard statistics...");
        long startTime = System.currentTimeMillis();

        for (ShardConfig shard : properties.getShards()) {
            analyzeShard(shard);
        }

        lastAnalyzeTime = System.currentTimeMillis();
        logger.info("Statistics refreshed in {} ms",
                System.currentTimeMillis() - startTime);
    }

    public void analyzeShard(ShardConfig shard) {
        ShardStatistics stats = shardStatsMap.computeIfAbsent(
                shard.getShardId(), ShardStatistics::new);

        JdbcTemplate jdbcTemplate = getShardJdbcTemplate(shard.getShardId());
        if (jdbcTemplate == null) {
            logger.warn("No JDBC template for shard: {}, using default estimates",
                    shard.getShardId());
            return;
        }

        try {
            Map<String, Long> tableCounts = getTableRowCounts(jdbcTemplate, shard);
            stats.setTableRowCounts(tableCounts);

            Map<String, Map<String, Long>> cardinalities = getColumnCardinalities(
                    jdbcTemplate, shard);
            stats.setColumnCardinalities(cardinalities);

            stats.setLastAnalyzeTime(System.currentTimeMillis());
            logger.debug("Analyzed shard {}: {} tables",
                    shard.getShardId(), tableCounts.size());

        } catch (Exception e) {
            logger.warn("Failed to analyze shard {}: {}",
                    shard.getShardId(), e.getMessage());
        }
    }

    private Map<String, Long> getTableRowCounts(JdbcTemplate jdbcTemplate, ShardConfig shard) {
        Map<String, Long> counts = new ConcurrentHashMap<>();

        try {
            String sql = "SELECT TABLE_NAME, TABLE_ROWS FROM information_schema.TABLES " +
                    "WHERE TABLE_SCHEMA = ?";
            jdbcTemplate.query(sql, rs -> {
                String tableName = rs.getString("TABLE_NAME");
                long rows = rs.getLong("TABLE_ROWS");
                counts.put(tableName, Math.max(1, rows));
            }, shard.getDatabase() != null ? shard.getDatabase() : shard.getSchema());

        } catch (Exception e) {
            logger.debug("Could not fetch table counts from information_schema: {}",
                    e.getMessage());
        }

        if (counts.isEmpty()) {
            counts.put("t_user", 1000000L);
            counts.put("t_order", 10000000L);
        }

        return counts;
    }

    private Map<String, Map<String, Long>> getColumnCardinalities(
            JdbcTemplate jdbcTemplate, ShardConfig shard) {

        Map<String, Map<String, Long>> cardinalities = new ConcurrentHashMap<>();

        try {
            String sql = "SELECT TABLE_NAME, COLUMN_NAME, CARDINALITY " +
                    "FROM information_schema.STATISTICS WHERE TABLE_SCHEMA = ?";

            jdbcTemplate.query(sql, rs -> {
                String tableName = rs.getString("TABLE_NAME");
                String columnName = rs.getString("COLUMN_NAME");
                long cardinality = rs.getLong("CARDINALITY");

                cardinalities.computeIfAbsent(tableName, k -> new ConcurrentHashMap<>())
                        .put(columnName, Math.max(1, cardinality));
            }, shard.getDatabase() != null ? shard.getDatabase() : shard.getSchema());

        } catch (Exception e) {
            logger.debug("Could not fetch column cardinalities: {}", e.getMessage());
        }

        return cardinalities;
    }

    public ShardStatistics getShardStatistics(String shardId) {
        return shardStatsMap.computeIfAbsent(shardId, ShardStatistics::new);
    }

    public long estimateRows(String shardId, String tableName,
                             String columnName, String operator, Object value) {
        ShardStatistics stats = getShardStatistics(shardId);
        long baseRows = stats.getTableRowCount(tableName);
        double selectivity = stats.getSelectivity(tableName, columnName, operator, value);
        return Math.max(1, (long) (baseRows * selectivity));
    }

    public long estimateJoinRows(
            String shardId,
            String leftTable, String leftColumn,
            String rightTable, String rightColumn) {

        ShardStatistics stats = getShardStatistics(shardId);
        long leftRows = stats.getTableRowCount(leftTable);
        long rightRows = stats.getTableRowCount(rightTable);
        long leftCard = stats.getColumnCardinality(leftTable, leftColumn);
        long rightCard = stats.getColumnCardinality(rightTable, rightColumn);

        double joinSelectivity = 1.0 / Math.max(leftCard, rightCard);
        return Math.max(1, (long) (leftRows * rightRows * joinSelectivity));
    }

    public double estimateQueryCost(String shardId, long estimatedRows) {
        ShardStatistics stats = getShardStatistics(shardId);
        double latency = stats.getAvgQueryLatencyMs() > 0
                ? stats.getAvgQueryLatencyMs() : 5.0;
        return estimatedRows * 0.001 + latency;
    }

    private JdbcTemplate getShardJdbcTemplate(String shardId) {
        if (shardJdbcTemplates == null) {
            return null;
        }
        return shardJdbcTemplates.get(shardId);
    }

    private boolean isStatisticsEnabled() {
        return properties.getJoin() == null
                || properties.getJoin().isStatsEnabled();
    }

    public void recordQueryLatency(String shardId, long latencyMs) {
        ShardStatistics stats = getShardStatistics(shardId);
        double currentAvg = stats.getAvgQueryLatencyMs();
        long count = stats.getQueryCount();

        double newAvg = (currentAvg * count + latencyMs) / (count + 1);
        stats.setAvgQueryLatencyMs(newAvg);
        stats.setQueryCount(count + 1);
    }

    public long getLastAnalyzeTime() {
        return lastAnalyzeTime;
    }

    public Set<String> getKnownShards() {
        return shardStatsMap.keySet();
    }
}
