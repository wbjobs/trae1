package com.sharding.sync.config;

import lombok.Data;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;

import java.util.ArrayList;
import java.util.List;

@Data
@Component
@ConfigurationProperties(prefix = "mycat")
public class MyCatProperties {

    private String adminUrl = "http://127.0.0.1:9066";
    private String username = "root";
    private String password = "123456";
    private String logicDatabase = "TESTDB";

    private SyncProperties sync = new SyncProperties();
    private BinlogProperties binlog = new BinlogProperties();

    private List<LogicTable> tables = new ArrayList<>();

    @Data
    public static class SyncProperties {
        private int threadPoolSize = 16;
        private int batchSize = 500;
        private int retryTimes = 3;
        private long retryIntervalMs = 2000L;
        private int fetchSize = 1000;
    }

    @Data
    public static class BinlogProperties {
        private boolean enabled = true;
        private String positionFile = "binlog-position.json";
        private long pollIntervalMs = 1000L;
    }

    @Data
    public static class LogicTable {
        private String name;
        private String shardingColumn;
        private String algorithm = "mod-long";
        private int shardCount = 2;
        private String primaryKey = "id";
        private List<String> shardNodes = new ArrayList<>();
    }
}
