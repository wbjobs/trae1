package com.alibaba.polardb.index.config;

import lombok.Data;

@Data
public class ShardConfig {
    private String id;
    private String name;
    private String canalServer;
    private String destination;
    private String username;
    private String password;
    private String dbUrl;
    private String dbUsername;
    private String dbPassword;
    private String shardKey;
    private String globalIdColumn;
    private String sourceTable;
    private int batchSize = 1024;
    private int soTimeout = 60000;
    private int idleTimeout = 60000;
    private int hashWeight = 100;
    private int status = 1;
    private String hashKey;

    public String getHashKey() {
        return hashKey != null ? hashKey : id;
    }
}
