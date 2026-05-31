package com.alibaba.polardb.index;

import com.alibaba.polardb.index.canal.MultiShardCanalListener;
import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.join.executor.QueryExecutor;
import com.alibaba.polardb.index.rebalance.IndexRebalanceService;
import com.alibaba.polardb.index.rebuild.FullRebuildService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.CommandLineRunner;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.ApplicationContext;
import org.springframework.scheduling.annotation.EnableAsync;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableAsync
@EnableScheduling
public class GlobalIndexSyncApplication implements CommandLineRunner {

    private static final Logger logger = LoggerFactory.getLogger(GlobalIndexSyncApplication.class);

    @Autowired
    private MultiShardCanalListener canalListener;

    @Autowired
    private FullRebuildService rebuildService;

    @Autowired(required = false)
    private IndexRebalanceService rebalanceService;

    @Autowired(required = false)
    private QueryExecutor queryExecutor;

    @Autowired
    private ApplicationContext applicationContext;

    @Autowired
    private GlobalIndexSyncProperties properties;

    public static void main(String[] args) {
        SpringApplication app = new SpringApplication(GlobalIndexSyncApplication.class);

        boolean rebuildAll = false;
        String rebuildShard = null;
        boolean rebalanceAll = false;
        String rebalanceShard = null;
        Integer rebalanceThreads = null;
        boolean rebalanceDryRun = false;
        boolean explainJoin = false;
        Long joinTimeoutMs = null;

        for (int i = 0; i < args.length; i++) {
            if ("--rebuild".equals(args[i])) {
                rebuildAll = true;
            } else if ("--rebuild-shard".equals(args[i]) && i + 1 < args.length) {
                rebuildShard = args[i + 1];
                i++;
            } else if ("--rebalance".equals(args[i])) {
                rebalanceAll = true;
            } else if ("--rebalance-shard".equals(args[i]) && i + 1 < args.length) {
                rebalanceShard = args[i + 1];
                i++;
            } else if ("--rebalance-threads".equals(args[i]) && i + 1 < args.length) {
                try {
                    rebalanceThreads = Integer.parseInt(args[i + 1]);
                    if (rebalanceThreads <= 0) {
                        logger.error("--rebalance-threads must be positive, using default value");
                        rebalanceThreads = null;
                    }
                } catch (NumberFormatException e) {
                    logger.error("Invalid --rebalance-threads value: {}", args[i + 1]);
                    System.exit(1);
                }
                i++;
            } else if ("--rebalance-dry-run".equals(args[i])) {
                rebalanceDryRun = true;
            } else if ("--explain".equals(args[i])) {
                explainJoin = true;
            } else if ("--join-timeout".equals(args[i]) && i + 1 < args.length) {
                try {
                    joinTimeoutMs = Long.parseLong(args[i + 1]);
                    if (joinTimeoutMs <= 0) {
                        logger.error("--join-timeout must be positive, using default value");
                        joinTimeoutMs = null;
                    }
                } catch (NumberFormatException e) {
                    logger.error("Invalid --join-timeout value: {}", args[i + 1]);
                    System.exit(1);
                }
                i++;
            } else if ("--help".equals(args[i]) || "-h".equals(args[i])) {
                printHelp();
                System.exit(0);
            }
        }

        if (explainJoin) {
            System.setProperty("join.explain", "true");
        }
        if (joinTimeoutMs != null) {
            System.setProperty("join.timeout-ms", String.valueOf(joinTimeoutMs));
        }

        if (rebuildAll || rebuildShard != null) {
            app.setAdditionalProfiles("rebuild");
        }

        System.setProperty("rebuild.all", String.valueOf(rebuildAll));
        if (rebuildShard != null) {
            System.setProperty("rebuild.shard", rebuildShard);
        }

        System.setProperty("rebalance.all", String.valueOf(rebalanceAll));
        if (rebalanceShard != null) {
            System.setProperty("rebalance.shard", rebalanceShard);
        }
        if (rebalanceThreads != null) {
            System.setProperty("rebalance.threads", String.valueOf(rebalanceThreads));
        }
        System.setProperty("rebalance.dry-run", String.valueOf(rebalanceDryRun));

        SpringApplication.run(GlobalIndexSyncApplication.class, args);
    }

    private static void printHelp() {
        System.out.println("PolarDB Global Index Sync Tool");
        System.out.println();
        System.out.println("Usage: java -jar polardb-global-index-sync.jar [options]");
        System.out.println();
        System.out.println("Options:");
        System.out.println("  --rebuild                 Full rebuild of global index from all shards");
        System.out.println("  --rebuild-shard <id>      Rebuild global index for specific shard");
        System.out.println("  --rebalance               Rebalance index for topology changes");
        System.out.println("  --rebalance-shard <id>    Rebalance index for specific shard");
        System.out.println("  --rebalance-threads <n>   Number of threads for rebalance (default: 8)");
        System.out.println("  --rebalance-dry-run       Dry run mode for rebalance");
        System.out.println("  --explain                 Output query execution plan with cost estimation");
        System.out.println("  --join-timeout <ms>       Join query timeout in milliseconds (default: 30000)");
        System.out.println("  -h, --help                Show this help message");
        System.out.println();
        System.out.println("Cross-Shard Join Features:");
        System.out.println("  - Automatic query optimization with cost estimation");
        System.out.println("  - Predicate pushdown to shards");
        System.out.println("  - Parallel sub-query execution across shards");
        System.out.println("  - Hash Join and Nested Loop Join algorithms");
        System.out.println("  - 5-second result cache for repeated queries");
        System.out.println("  - Join timeout (default: 30 seconds)");
        System.out.println();
        System.out.println("Examples:");
        System.out.println("  # Normal incremental sync mode");
        System.out.println("  java -jar polardb-global-index-sync.jar");
        System.out.println();
        System.out.println("  # Full rebuild");
        System.out.println("  java -jar polardb-global-index-sync.jar --rebuild");
        System.out.println();
        System.out.println("  # Rebalance with 16 threads");
        System.out.println("  java -jar polardb-global-index-sync.jar --rebalance --rebalance-threads 16");
        System.out.println();
        System.out.println("  # Start with join query timeout of 60 seconds");
        System.out.println("  java -jar polardb-global-index-sync.jar --join-timeout 60000");
        System.out.println();
        System.out.println("Join API Endpoints:");
        System.out.println("  POST /api/v1/global-index/join/execute           Execute cross-shard join query");
        System.out.println("  POST /api/v1/global-index/join/execute?explain=true  Execute with execution plan");
        System.out.println("  POST /api/v1/global-index/join/explain           Only show execution plan");
        System.out.println("  GET  /api/v1/global-index/join/cache/status      Check query cache status");
        System.out.println("  POST /api/v1/global-index/join/cache/invalidate  Clear query cache");
        System.out.println("  GET  /api/v1/global-index/join/stats             Get join engine statistics");
        System.out.println("  POST /api/v1/global-index/join/example           Get example query format");
        System.out.println();
    }

    @Override
    public void run(String... args) throws Exception {
        logger.info("==================================================");
        logger.info("  PolarDB Global Index Sync Tool Starting...");
        logger.info("==================================================");

        String rebalanceThreadsStr = System.getProperty("rebalance.threads");
        if (rebalanceThreadsStr != null && rebalanceService != null) {
            int threads = Integer.parseInt(rebalanceThreadsStr);
            try {
                rebalanceService.setRebalanceThreads(threads);
                logger.info("Rebalance thread pool size set to: {}", threads);
            } catch (Exception e) {
                logger.warn("Failed to set rebalance threads: {}", e.getMessage());
            }
        }

        boolean rebuildAll = Boolean.getBoolean("rebuild.all");
        String rebuildShard = System.getProperty("rebuild.shard");

        if (rebuildAll) {
            logger.info("Starting full index rebuild...");
            boolean success = rebuildService.rebuildAll();
            if (success) {
                logger.info("Full index rebuild completed successfully!");
            } else {
                logger.error("Full index rebuild failed!");
                System.exit(1);
            }
        } else if (rebuildShard != null && !rebuildShard.isEmpty()) {
            logger.info("Starting rebuild for shard: {}", rebuildShard);
            boolean success = rebuildService.rebuildShard(rebuildShard);
            if (success) {
                logger.info("Shard {} rebuild completed successfully!", rebuildShard);
            } else {
                logger.error("Shard {} rebuild failed!", rebuildShard);
                System.exit(1);
            }
        }

        boolean rebalanceAll = Boolean.getBoolean("rebalance.all");
        String rebalanceShard = System.getProperty("rebalance.shard");
        boolean rebalanceDryRun = Boolean.getBoolean("rebalance.dry-run");

        if (rebalanceService != null) {
            if (rebalanceAll) {
                logger.info("Starting full index rebalance...");
                if (rebalanceDryRun) {
                    logger.info("Dry run mode enabled, no actual changes will be made");
                }
                boolean success = rebalanceService.triggerFullRebalance();
                if (success) {
                    logger.info("Full index rebalance started successfully!");
                } else {
                    logger.error("Full index rebalance failed to start!");
                    System.exit(1);
                }
            } else if (rebalanceShard != null && !rebalanceShard.isEmpty()) {
                logger.info("Starting rebalance for shard: {}", rebalanceShard);
                if (rebalanceDryRun) {
                    logger.info("Dry run mode enabled, no actual changes will be made");
                }
                boolean success = rebalanceService.triggerShardRebalance(rebalanceShard);
                if (success) {
                    logger.info("Shard {} rebalance started successfully!", rebalanceShard);
                } else {
                    logger.error("Shard {} rebalance failed to start!", rebalanceShard);
                    System.exit(1);
                }
            }
        }

        if (!rebuildAll && !rebalanceAll) {
            logger.info("Starting Canal listeners for all shards...");
            canalListener.startAllListeners();
            logger.info("Canal listeners started successfully!");
        }

        String joinTimeoutStr = System.getProperty("join.timeout-ms");
        if (joinTimeoutStr != null && properties.getJoin() != null) {
            properties.getJoin().setTimeoutMs(Long.parseLong(joinTimeoutStr));
            logger.info("Join timeout set to: {} ms", joinTimeoutStr);
        }

        boolean explainMode = Boolean.getBoolean("join.explain");
        if (explainMode) {
            logger.info("Explain mode enabled - join queries will show execution plans");
        }

        if (!rebuildAll && !rebalanceAll) {
            logger.info("Starting Canal listeners for all shards...");
            canalListener.startAllListeners();
            logger.info("Canal listeners started successfully!");
        }

        logger.info("==================================================");
        logger.info("  PolarDB Global Index Sync Tool Started!");
        logger.info("  HTTP API: http://localhost:8080/api/v1/global-index");
        logger.info("  Prometheus: http://localhost:8080/actuator/prometheus");
        logger.info("==================================================");
        logger.info("  Rebalance API:");
        logger.info("    POST /api/v1/global-index/rebalance");
        logger.info("    GET  /api/v1/global-index/rebalance/status");
        logger.info("    POST /api/v1/global-index/rebalance/stop");
        logger.info("    POST /api/v1/global-index/rebalance/threads?threads=N");
        logger.info("    GET  /api/v1/global-index/topology");
        logger.info("    GET  /api/v1/global-index/hash/calculate?globalId=X");
        logger.info("==================================================");
        logger.info("  Cross-Shard Join API:");
        logger.info("    POST /api/v1/global-index/join/execute");
        logger.info("    POST /api/v1/global-index/join/execute?explain=true");
        logger.info("    POST /api/v1/global-index/join/explain");
        logger.info("    GET  /api/v1/global-index/join/cache/status");
        logger.info("    POST /api/v1/global-index/join/cache/invalidate");
        logger.info("    GET  /api/v1/global-index/join/stats");
        logger.info("    POST /api/v1/global-index/join/example");
        logger.info("==================================================");
        if (properties.getJoin() != null && properties.getJoin().isEnabled()) {
            logger.info("  Join Configuration:");
            logger.info("    Enabled: true");
            logger.info("    Timeout: {} ms", properties.getJoin().getTimeoutMs());
            logger.info("    Algorithm: {}", properties.getJoin().getDefaultJoinAlgorithm());
            logger.info("    Predicate Pushdown: {}", properties.getJoin().isPredicatePushdownEnabled());
            logger.info("    Result Cache: {} (TTL: {}s)",
                    properties.getJoin().isResultCacheEnabled(),
                    properties.getJoin().getCacheTtlSeconds());
            logger.info("    Hash Join Threshold: {} rows", properties.getJoin().getHashJoinThreshold());
        } else {
            logger.info("  Cross-shard join is disabled");
        }
        logger.info("==================================================");
    }
}
