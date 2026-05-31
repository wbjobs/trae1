package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class IndexTableConfig {
    private String schema;
    private String table;
    private String shardKeyColumn = "shard_key";
    private String globalIdColumn = "global_id";
    private String shardIdColumn = "shard_id";
    private String createTimeColumn = "gmt_create";
    private String updateTimeColumn = "gmt_modified";

    public String getFullTableName() {
        return schema + "." + table;
    }
}
