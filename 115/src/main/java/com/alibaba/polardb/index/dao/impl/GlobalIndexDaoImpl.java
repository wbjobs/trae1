package com.alibaba.polardb.index.dao.impl;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.IndexTableConfig;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.dao.EmptyResultDataAccessException;
import org.springframework.jdbc.core.BatchPreparedStatementSetter;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.jdbc.core.namedparam.NamedParameterJdbcTemplate;
import org.springframework.stereotype.Repository;

import javax.annotation.PostConstruct;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Timestamp;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

@Repository
public class GlobalIndexDaoImpl implements GlobalIndexDao {

    private static final Logger logger = LoggerFactory.getLogger(GlobalIndexDaoImpl.class);

    @Autowired
    @Qualifier("centerJdbcTemplate")
    private JdbcTemplate jdbcTemplate;

    @Autowired
    @Qualifier("centerNamedParameterJdbcTemplate")
    private NamedParameterJdbcTemplate namedParameterJdbcTemplate;

    @Autowired
    private GlobalIndexSyncProperties properties;

    private IndexTableConfig tableConfig;
    private String baseInsertSql;
    private String baseUpsertSql;
    private String baseUpdateSql;

    private Cache<String, GlobalIndex> cache;

    @PostConstruct
    public void init() {
        tableConfig = properties.getIndexTable();
        buildSqlStatements();

        cache = CacheBuilder.newBuilder()
                .maximumSize(100000)
                .expireAfterWrite(5, TimeUnit.MINUTES)
                .recordStats()
                .build();

        createIndexTableIfNotExists();
    }

    private void buildSqlStatements() {
        String table = tableConfig.getFullTableName();
        String globalIdCol = tableConfig.getGlobalIdColumn();
        String shardKeyCol = tableConfig.getShardKeyColumn();
        String shardIdCol = tableConfig.getShardIdColumn();
        String createCol = tableConfig.getCreateTimeColumn();
        String updateCol = tableConfig.getUpdateTimeColumn();

        baseInsertSql = String.format(
                "INSERT INTO %s (%s, %s, %s, %s, %s) VALUES (?, ?, ?, ?, ?)",
                table, globalIdCol, shardKeyCol, shardIdCol, createCol, updateCol
        );

        baseUpsertSql = String.format(
                "INSERT INTO %s (%s, %s, %s, %s, %s) VALUES (?, ?, ?, ?, ?) " +
                        "ON DUPLICATE KEY UPDATE %s = VALUES(%s), %s = VALUES(%s), %s = VALUES(%s)",
                table, globalIdCol, shardKeyCol, shardIdCol, createCol, updateCol,
                shardKeyCol, shardKeyCol, shardIdCol, shardIdCol, updateCol, updateCol
        );

        baseUpdateSql = String.format(
                "UPDATE %s SET %s = ?, %s = ?, %s = ? WHERE %s = ?",
                table, shardKeyCol, shardIdCol, updateCol, globalIdCol
        );
    }

    @Override
    public GlobalIndex findByGlobalId(String globalId) {
        if (globalId == null) {
            return null;
        }

        GlobalIndex cached = cache.getIfPresent(globalId);
        if (cached != null) {
            return cached;
        }

        String sql = String.format(
                "SELECT %s, %s, %s, %s, %s FROM %s WHERE %s = ? LIMIT 1",
                tableConfig.getGlobalIdColumn(), tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(), tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getFullTableName(),
                tableConfig.getGlobalIdColumn()
        );

        try {
            GlobalIndex index = jdbcTemplate.queryForObject(sql, new Object[]{globalId}, new GlobalIndexRowMapper());
            if (index != null) {
                cache.put(globalId, index);
            }
            return index;
        } catch (EmptyResultDataAccessException e) {
            return null;
        }
    }

    @Override
    public List<GlobalIndex> findByShardKey(String shardKey) {
        if (shardKey == null) {
            return Collections.emptyList();
        }

        String sql = String.format(
                "SELECT %s, %s, %s, %s, %s FROM %s WHERE %s = ?",
                tableConfig.getGlobalIdColumn(), tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(), tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getFullTableName(),
                tableConfig.getShardKeyColumn()
        );

        return jdbcTemplate.query(sql, new Object[]{shardKey}, new GlobalIndexRowMapper());
    }

    @Override
    public List<GlobalIndex> findByShardId(String shardId) {
        if (shardId == null) {
            return Collections.emptyList();
        }

        String sql = String.format(
                "SELECT %s, %s, %s, %s, %s FROM %s WHERE %s = ?",
                tableConfig.getGlobalIdColumn(), tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(), tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getFullTableName(),
                tableConfig.getShardIdColumn()
        );

        return jdbcTemplate.query(sql, new Object[]{shardId}, new GlobalIndexRowMapper());
    }

    @Override
    public int insert(GlobalIndex globalIndex) {
        Date now = new Date();
        if (globalIndex.getGmtCreate() == null) {
            globalIndex.setGmtCreate(now);
        }
        if (globalIndex.getGmtModified() == null) {
            globalIndex.setGmtModified(now);
        }

        int result = jdbcTemplate.update(baseInsertSql,
                globalIndex.getGlobalId(),
                globalIndex.getShardKey(),
                globalIndex.getShardId(),
                new Timestamp(globalIndex.getGmtCreate().getTime()),
                new Timestamp(globalIndex.getGmtModified().getTime()));

        cache.invalidate(globalIndex.getGlobalId());
        return result;
    }

    @Override
    public int batchInsert(List<GlobalIndex> globalIndices) {
        if (globalIndices == null || globalIndices.isEmpty()) {
            return 0;
        }

        Date now = new Date();
        int[] result = jdbcTemplate.batchUpdate(baseInsertSql, new BatchPreparedStatementSetter() {
            @Override
            public void setValues(PreparedStatement ps, int i) throws SQLException {
                GlobalIndex index = globalIndices.get(i);
                Date createTime = index.getGmtCreate() != null ? index.getGmtCreate() : now;
                Date modifyTime = index.getGmtModified() != null ? index.getGmtModified() : now;

                ps.setString(1, index.getGlobalId());
                ps.setString(2, index.getShardKey());
                ps.setString(3, index.getShardId());
                ps.setTimestamp(4, new Timestamp(createTime.getTime()));
                ps.setTimestamp(5, new Timestamp(modifyTime.getTime()));
            }

            @Override
            public int getBatchSize() {
                return globalIndices.size();
            }
        });

        for (GlobalIndex index : globalIndices) {
            cache.invalidate(index.getGlobalId());
        }

        return Arrays.stream(result).sum();
    }

    @Override
    public int update(GlobalIndex globalIndex) {
        Date now = new Date();
        if (globalIndex.getGmtModified() == null) {
            globalIndex.setGmtModified(now);
        }

        int result = jdbcTemplate.update(baseUpdateSql,
                globalIndex.getShardKey(),
                globalIndex.getShardId(),
                new Timestamp(globalIndex.getGmtModified().getTime()),
                globalIndex.getGlobalId());

        cache.invalidate(globalIndex.getGlobalId());
        return result;
    }

    @Override
    public int upsert(GlobalIndex globalIndex) {
        Date now = new Date();
        if (globalIndex.getGmtCreate() == null) {
            globalIndex.setGmtCreate(now);
        }
        if (globalIndex.getGmtModified() == null) {
            globalIndex.setGmtModified(now);
        }

        int result = jdbcTemplate.update(baseUpsertSql,
                globalIndex.getGlobalId(),
                globalIndex.getShardKey(),
                globalIndex.getShardId(),
                new Timestamp(globalIndex.getGmtCreate().getTime()),
                new Timestamp(globalIndex.getGmtModified().getTime()));

        cache.invalidate(globalIndex.getGlobalId());
        return result;
    }

    @Override
    public int batchUpsert(List<GlobalIndex> globalIndices) {
        if (globalIndices == null || globalIndices.isEmpty()) {
            return 0;
        }

        Date now = new Date();
        int[] result = jdbcTemplate.batchUpdate(baseUpsertSql, new BatchPreparedStatementSetter() {
            @Override
            public void setValues(PreparedStatement ps, int i) throws SQLException {
                GlobalIndex index = globalIndices.get(i);
                Date createTime = index.getGmtCreate() != null ? index.getGmtCreate() : now;
                Date modifyTime = index.getGmtModified() != null ? index.getGmtModified() : now;

                ps.setString(1, index.getGlobalId());
                ps.setString(2, index.getShardKey());
                ps.setString(3, index.getShardId());
                ps.setTimestamp(4, new Timestamp(createTime.getTime()));
                ps.setTimestamp(5, new Timestamp(modifyTime.getTime()));
            }

            @Override
            public int getBatchSize() {
                return globalIndices.size();
            }
        });

        for (GlobalIndex index : globalIndices) {
            cache.invalidate(index.getGlobalId());
        }

        return Arrays.stream(result).sum();
    }

    @Override
    public int deleteByGlobalId(String globalId) {
        if (globalId == null) {
            return 0;
        }

        String sql = String.format("DELETE FROM %s WHERE %s = ?",
                tableConfig.getFullTableName(), tableConfig.getGlobalIdColumn());

        int result = jdbcTemplate.update(sql, globalId);
        cache.invalidate(globalId);
        return result;
    }

    @Override
    public int deleteByShardId(String shardId) {
        if (shardId == null) {
            return 0;
        }

        String sql = String.format("DELETE FROM %s WHERE %s = ?",
                tableConfig.getFullTableName(), tableConfig.getShardIdColumn());

        int result = jdbcTemplate.update(sql, shardId);
        cache.invalidateAll();
        return result;
    }

    @Override
    public long count() {
        String sql = String.format("SELECT COUNT(*) FROM %s", tableConfig.getFullTableName());
        Long count = jdbcTemplate.queryForObject(sql, Long.class);
        return count != null ? count : 0;
    }

    @Override
    public long countByShardId(String shardId) {
        if (shardId == null) {
            return 0;
        }
        String sql = String.format("SELECT COUNT(*) FROM %s WHERE %s = ?",
                tableConfig.getFullTableName(), tableConfig.getShardIdColumn());
        Long count = jdbcTemplate.queryForObject(sql, Long.class, shardId);
        return count != null ? count : 0;
    }

    @Override
    public void createIndexTableIfNotExists() {
        String sql = String.format(
                "CREATE TABLE IF NOT EXISTS %s (\n" +
                        "  %s VARCHAR(128) NOT NULL COMMENT '全局唯一ID',\n" +
                        "  %s VARCHAR(128) NOT NULL COMMENT '分片键',\n" +
                        "  %s VARCHAR(64) NOT NULL COMMENT '分片ID',\n" +
                        "  %s DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',\n" +
                        "  %s DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',\n" +
                        "  PRIMARY KEY (%s),\n" +
                        "  KEY idx_shard_key (%s),\n" +
                        "  KEY idx_shard_id (%s)\n" +
                        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_bin COMMENT='全局索引表'",
                tableConfig.getFullTableName(),
                tableConfig.getGlobalIdColumn(),
                tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(),
                tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getGlobalIdColumn(),
                tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn()
        );

        jdbcTemplate.execute(sql);
        logger.info("Global index table {} created or already exists", tableConfig.getFullTableName());
    }

    @Override
    public void truncateIndexTable() {
        String sql = String.format("TRUNCATE TABLE %s", tableConfig.getFullTableName());
        jdbcTemplate.execute(sql);
        cache.invalidateAll();
        logger.info("Global index table {} truncated", tableConfig.getFullTableName());
    }

    @Override
    public List<GlobalIndex> findAllWithPagination(int offset, int limit) {
        String sql = String.format(
                "SELECT %s, %s, %s, %s, %s FROM %s ORDER BY %s LIMIT ?, ?",
                tableConfig.getGlobalIdColumn(), tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(), tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getFullTableName(),
                tableConfig.getGlobalIdColumn()
        );
        return jdbcTemplate.query(sql, new Object[]{offset, limit}, new GlobalIndexRowMapper());
    }

    @Override
    public List<GlobalIndex> findByGlobalIdRange(String startGlobalId, String endGlobalId, int limit) {
        StringBuilder sql = new StringBuilder();
        List<Object> params = new ArrayList<>();

        sql.append(String.format(
                "SELECT %s, %s, %s, %s, %s FROM %s WHERE 1=1",
                tableConfig.getGlobalIdColumn(), tableConfig.getShardKeyColumn(),
                tableConfig.getShardIdColumn(), tableConfig.getCreateTimeColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getFullTableName()
        ));

        if (startGlobalId != null && !startGlobalId.isEmpty()) {
            sql.append(" AND ").append(tableConfig.getGlobalIdColumn()).append(" > ?");
            params.add(startGlobalId);
        }
        if (endGlobalId != null && !endGlobalId.isEmpty()) {
            sql.append(" AND ").append(tableConfig.getGlobalIdColumn()).append(" <= ?");
            params.add(endGlobalId);
        }

        sql.append(" ORDER BY ").append(tableConfig.getGlobalIdColumn()).append(" LIMIT ?");
        params.add(limit);

        return jdbcTemplate.query(sql.toString(), params.toArray(), new GlobalIndexRowMapper());
    }

    @Override
    public int batchUpdateShardId(List<Map<String, String>> updates) {
        if (updates == null || updates.isEmpty()) {
            return 0;
        }

        String sql = String.format(
                "UPDATE %s SET %s = ?, %s = ? WHERE %s = ?",
                tableConfig.getFullTableName(),
                tableConfig.getShardIdColumn(),
                tableConfig.getUpdateTimeColumn(),
                tableConfig.getGlobalIdColumn()
        );

        Date now = new Date();
        int[] result = jdbcTemplate.batchUpdate(sql, new BatchPreparedStatementSetter() {
            @Override
            public void setValues(PreparedStatement ps, int i) throws SQLException {
                Map<String, String> update = updates.get(i);
                ps.setString(1, update.get("newShardId"));
                ps.setTimestamp(2, new Timestamp(now.getTime()));
                ps.setString(3, update.get("globalId"));
            }

            @Override
            public int getBatchSize() {
                return updates.size();
            }
        });

        for (Map<String, String> update : updates) {
            cache.invalidate(update.get("globalId"));
        }

        return Arrays.stream(result).sum();
    }

    @Override
    public List<String> findAllShardIds() {
        String sql = String.format(
                "SELECT DISTINCT %s FROM %s WHERE %s IS NOT NULL",
                tableConfig.getShardIdColumn(),
                tableConfig.getFullTableName(),
                tableConfig.getShardIdColumn()
        );
        return jdbcTemplate.queryForList(sql, String.class);
    }

    @Override
    public long countMismatchedRecords(String expectedShardId, String actualShardId) {
        String sql = String.format(
                "SELECT COUNT(*) FROM %s WHERE %s = ? AND %s != ?",
                tableConfig.getFullTableName(),
                tableConfig.getShardIdColumn(),
                tableConfig.getShardIdColumn()
        );
        Long count = jdbcTemplate.queryForObject(sql, Long.class, expectedShardId, actualShardId);
        return count != null ? count : 0;
    }

    private class GlobalIndexRowMapper implements RowMapper<GlobalIndex> {
        @Override
        public GlobalIndex mapRow(ResultSet rs, int rowNum) throws SQLException {
            return GlobalIndex.builder()
                    .globalId(rs.getString(tableConfig.getGlobalIdColumn()))
                    .shardKey(rs.getString(tableConfig.getShardKeyColumn()))
                    .shardId(rs.getString(tableConfig.getShardIdColumn()))
                    .gmtCreate(rs.getTimestamp(tableConfig.getCreateTimeColumn()))
                    .gmtModified(rs.getTimestamp(tableConfig.getUpdateTimeColumn()))
                    .build();
        }
    }
}
