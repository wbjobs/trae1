package com.alibaba.polardb.index.dao;

import com.alibaba.polardb.index.model.GlobalIndex;

import java.util.List;
import java.util.Map;

public interface GlobalIndexDao {

    GlobalIndex findByGlobalId(String globalId);

    List<GlobalIndex> findByShardKey(String shardKey);

    List<GlobalIndex> findByShardId(String shardId);

    int insert(GlobalIndex globalIndex);

    int batchInsert(List<GlobalIndex> globalIndices);

    int update(GlobalIndex globalIndex);

    int upsert(GlobalIndex globalIndex);

    int batchUpsert(List<GlobalIndex> globalIndices);

    int deleteByGlobalId(String globalId);

    int deleteByShardId(String shardId);

    long count();

    long countByShardId(String shardId);

    void createIndexTableIfNotExists();

    void truncateIndexTable();

    List<GlobalIndex> findAllWithPagination(int offset, int limit);

    List<GlobalIndex> findByGlobalIdRange(String startGlobalId, String endGlobalId, int limit);

    int batchUpdateShardId(List<Map<String, String>> updates);

    List<String> findAllShardIds();

    long countMismatchedRecords(String expectedShardId, String actualShardId);
}
