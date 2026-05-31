package com.profile.config;

import java.io.Serializable;

public class AppConfig implements Serializable {

    private static final long serialVersionUID = 1L;

    private String kafkaBrokers = "localhost:9092";
    private String kafkaTopic = "user_behavior";
    private String kafkaGroupId = "profile-consumer-group";

    private String redisHost = "localhost";
    private int redisPort = 6379;
    private String redisPassword = "";
    private int redisDatabase = 0;
    private String redisKeyPrefix = "user:profile:";

    private int httpPort = 8080;

    private long stateTtlDays = 180;

    private long snapshotIntervalMinutes = 10080;

    private String newRedisHost = "";
    private int newRedisPort = 6379;
    private String newRedisPassword = "";
    private int newRedisDatabase = 1;
    private String newRedisKeyPrefix = "user:profile:new:";

    private volatile boolean useNewRedis = false;

    public String getKafkaBrokers() { return kafkaBrokers; }
    public void setKafkaBrokers(String kafkaBrokers) { this.kafkaBrokers = kafkaBrokers; }

    public String getKafkaTopic() { return kafkaTopic; }
    public void setKafkaTopic(String kafkaTopic) { this.kafkaTopic = kafkaTopic; }

    public String getKafkaGroupId() { return kafkaGroupId; }
    public void setKafkaGroupId(String kafkaGroupId) { this.kafkaGroupId = kafkaGroupId; }

    public String getRedisHost() { return redisHost; }
    public void setRedisHost(String redisHost) { this.redisHost = redisHost; }

    public int getRedisPort() { return redisPort; }
    public void setRedisPort(int redisPort) { this.redisPort = redisPort; }

    public String getRedisPassword() { return redisPassword; }
    public void setRedisPassword(String redisPassword) { this.redisPassword = redisPassword; }

    public int getRedisDatabase() { return redisDatabase; }
    public void setRedisDatabase(int redisDatabase) { this.redisDatabase = redisDatabase; }

    public String getRedisKeyPrefix() { return redisKeyPrefix; }
    public void setRedisKeyPrefix(String redisKeyPrefix) { this.redisKeyPrefix = redisKeyPrefix; }

    public int getHttpPort() { return httpPort; }
    public void setHttpPort(int httpPort) { this.httpPort = httpPort; }

    public long getStateTtlDays() { return stateTtlDays; }
    public void setStateTtlDays(long stateTtlDays) { this.stateTtlDays = stateTtlDays; }

    public long getSnapshotIntervalMinutes() { return snapshotIntervalMinutes; }
    public void setSnapshotIntervalMinutes(long snapshotIntervalMinutes) {
        this.snapshotIntervalMinutes = snapshotIntervalMinutes;
    }

    public String getNewRedisHost() { return newRedisHost; }
    public void setNewRedisHost(String newRedisHost) { this.newRedisHost = newRedisHost; }

    public int getNewRedisPort() { return newRedisPort; }
    public void setNewRedisPort(int newRedisPort) { this.newRedisPort = newRedisPort; }

    public String getNewRedisPassword() { return newRedisPassword; }
    public void setNewRedisPassword(String newRedisPassword) { this.newRedisPassword = newRedisPassword; }

    public int getNewRedisDatabase() { return newRedisDatabase; }
    public void setNewRedisDatabase(int newRedisDatabase) { this.newRedisDatabase = newRedisDatabase; }

    public String getNewRedisKeyPrefix() { return newRedisKeyPrefix; }
    public void setNewRedisKeyPrefix(String newRedisKeyPrefix) { this.newRedisKeyPrefix = newRedisKeyPrefix; }

    public boolean isUseNewRedis() { return useNewRedis; }
    public void setUseNewRedis(boolean useNewRedis) { this.useNewRedis = useNewRedis; }
}
