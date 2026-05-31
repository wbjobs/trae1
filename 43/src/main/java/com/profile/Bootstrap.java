package com.profile;

import com.profile.config.AppConfig;
import com.profile.config.RecalcConfig;
import com.profile.flink.ProfileFlinkJob;
import com.profile.flink.ProfileRecalcJob;
import com.profile.http.HttpServer;
import com.profile.service.RedisSwitchManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Bootstrap {

    private static final Logger log = LoggerFactory.getLogger(Bootstrap.class);

    public static void main(String[] args) {
        AppConfig config = loadAppConfig(args);
        RecalcConfig recalcConfig = loadRecalcConfig(args);

        String mode = System.getProperty("mode", "all");
        log.info("Starting in mode: {}", mode);

        switch (mode) {
            case "flink":
                runFlink(config);
                break;
            case "http":
                runHttp(config);
                break;
            case "recalc":
                runRecalc(config, recalcConfig);
                break;
            case "all":
            default:
                runAll(config);
                break;
        }
    }

    private static AppConfig loadAppConfig(String[] args) {
        AppConfig config = new AppConfig();

        String brokers = System.getProperty("kafka.brokers");
        if (brokers != null) config.setKafkaBrokers(brokers);

        String topic = System.getProperty("kafka.topic");
        if (topic != null) config.setKafkaTopic(topic);

        String groupId = System.getProperty("kafka.groupId");
        if (groupId != null) config.setKafkaGroupId(groupId);

        String redisHost = System.getProperty("redis.host");
        if (redisHost != null) config.setRedisHost(redisHost);

        String redisPort = System.getProperty("redis.port");
        if (redisPort != null) config.setRedisPort(Integer.parseInt(redisPort));

        String redisPassword = System.getProperty("redis.password");
        if (redisPassword != null) config.setRedisPassword(redisPassword);

        String redisDb = System.getProperty("redis.database");
        if (redisDb != null) config.setRedisDatabase(Integer.parseInt(redisDb));

        String httpPort = System.getProperty("http.port");
        if (httpPort != null) config.setHttpPort(Integer.parseInt(httpPort));

        String ttlDays = System.getProperty("state.ttl.days");
        if (ttlDays != null) config.setStateTtlDays(Long.parseLong(ttlDays));

        String snapshotMin = System.getProperty("snapshot.interval.minutes");
        if (snapshotMin != null) config.setSnapshotIntervalMinutes(Long.parseLong(snapshotMin));

        String newRedisHost = System.getProperty("redis.new.host");
        if (newRedisHost != null) config.setNewRedisHost(newRedisHost);

        String newRedisPort = System.getProperty("redis.new.port");
        if (newRedisPort != null) config.setNewRedisPort(Integer.parseInt(newRedisPort));

        String newRedisPassword = System.getProperty("redis.new.password");
        if (newRedisPassword != null) config.setNewRedisPassword(newRedisPassword);

        String newRedisDb = System.getProperty("redis.new.database");
        if (newRedisDb != null) config.setNewRedisDatabase(Integer.parseInt(newRedisDb));

        String newKeyPrefix = System.getProperty("redis.new.prefix");
        if (newKeyPrefix != null) config.setNewRedisKeyPrefix(newKeyPrefix);

        return config;
    }

    private static RecalcConfig loadRecalcConfig(String[] args) {
        RecalcConfig config = new RecalcConfig();

        String startTime = System.getProperty("recalc.start.ms");
        if (startTime != null) config.setStartTimestamp(Long.parseLong(startTime));

        String endTime = System.getProperty("recalc.end.ms");
        if (endTime != null) config.setEndTimestamp(Long.parseLong(endTime));

        String savepoint = System.getProperty("recalc.savepoint");
        if (savepoint != null) config.setSavepointPath(savepoint);

        String newHost = System.getProperty("recalc.redis.host");
        if (newHost != null) config.setNewRedisHost(newHost);

        String newPort = System.getProperty("recalc.redis.port");
        if (newPort != null) config.setNewRedisPort(Integer.parseInt(newPort));

        String newPass = System.getProperty("recalc.redis.password");
        if (newPass != null) config.setNewRedisPassword(newPass);

        String newDb = System.getProperty("recalc.redis.database");
        if (newDb != null) config.setNewRedisDatabase(Integer.parseInt(newDb));

        String newPrefix = System.getProperty("recalc.redis.prefix");
        if (newPrefix != null) config.setNewKeyPrefix(newPrefix);

        return config;
    }

    private static void runFlink(AppConfig config) {
        try {
            new ProfileFlinkJob(config).execute();
        } catch (Exception e) {
            log.error("Flink job failed", e);
            System.exit(1);
        }
    }

    private static void runHttp(AppConfig config) {
        RedisSwitchManager switchManager = new RedisSwitchManager(config);
        HttpServer server = new HttpServer(config, switchManager);
        server.start();

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            log.info("Shutting down HTTP server...");
            server.stop();
            switchManager.close();
        }));
    }

    private static void runRecalc(AppConfig config, RecalcConfig recalcConfig) {
        if (recalcConfig.getStartTimestamp() <= 0) {
            log.warn("recalc.start.ms not set, defaulting to 7 days ago");
            long sevenDaysAgo = System.currentTimeMillis() - 7L * 24 * 60 * 60 * 1000;
            recalcConfig.setStartTimestamp(sevenDaysAgo);
        }
        if (recalcConfig.getEndTimestamp() <= 0) {
            recalcConfig.setEndTimestamp(System.currentTimeMillis());
        }

        log.info("Starting recalc job: start={}, end={}, savepoint={}",
                recalcConfig.getStartTimestamp(),
                recalcConfig.getEndTimestamp(),
                recalcConfig.getSavepointPath());

        try {
            new ProfileRecalcJob(config, recalcConfig).execute();
        } catch (Exception e) {
            log.error("Recalc job failed", e);
            System.exit(1);
        }
    }

    private static void runAll(AppConfig config) {
        runHttp(config);
        runFlink(config);
    }
}
