package com.profile.store.sharding;

import java.util.ArrayList;
import java.util.List;

/**
 * 分片配置：定义分片数量、分片地址映射、Sentinel配置。
 * <p>
 * 分片路由使用一致性哈希（user_id mod shardCount）保证对应用透明。
 */
public class ShardProperties {

    private int shardCount = 64;
    private List<ShardNode> nodes = new ArrayList<>();
    private Sentinel sentinel = new Sentinel();
    private int parallelism = 16;

    public int getShardCount() { return shardCount; }
    public void setShardCount(int shardCount) { this.shardCount = shardCount; }

    public List<ShardNode> getNodes() { return nodes; }
    public void setNodes(List<ShardNode> nodes) { this.nodes = nodes; }

    public Sentinel getSentinel() { return sentinel; }
    public void setSentinel(Sentinel sentinel) { this.sentinel = sentinel; }

    public int getParallelism() { return parallelism; }
    public void setParallelism(int parallelism) { this.parallelism = parallelism; }

    public static class ShardNode {
        private int shardId;
        private String host = "localhost";
        private int port = 6379;
        private String password;
        private int database = 0;
        private boolean active = true;

        public int getShardId() { return shardId; }
        public void setShardId(int shardId) { this.shardId = shardId; }

        public String getHost() { return host; }
        public void setHost(String host) { this.host = host; }

        public int getPort() { return port; }
        public void setPort(int port) { this.port = port; }

        public String getPassword() { return password; }
        public void setPassword(String password) { this.password = password; }

        public int getDatabase() { return database; }
        public void setDatabase(int database) { this.database = database; }

        public boolean isActive() { return active; }
        public void setActive(boolean active) { this.active = active; }
    }

    public static class Sentinel {
        private boolean enabled = false;
        private String masterName = "mymaster";
        private List<String> nodes = new ArrayList<>();
        private String password;
        private int checkIntervalMs = 5000;
        private int failoverTimeoutMs = 10000;

        public boolean isEnabled() { return enabled; }
        public void setEnabled(boolean enabled) { this.enabled = enabled; }

        public String getMasterName() { return masterName; }
        public void setMasterName(String masterName) { this.masterName = masterName; }

        public List<String> getNodes() { return nodes; }
        public void setNodes(List<String> nodes) { this.nodes = nodes; }

        public String getPassword() { return password; }
        public void setPassword(String password) { this.password = password; }

        public int getCheckIntervalMs() { return checkIntervalMs; }
        public void setCheckIntervalMs(int checkIntervalMs) { this.checkIntervalMs = checkIntervalMs; }

        public int getFailoverTimeoutMs() { return failoverTimeoutMs; }
        public void setFailoverTimeoutMs(int failoverTimeoutMs) { this.failoverTimeoutMs = failoverTimeoutMs; }
    }
}
