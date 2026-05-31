package com.alibaba.polardb.index.rebalance;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.RebalanceConfig;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.hash.ConsistentHash;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.alibaba.polardb.index.monitor.SyncMetrics;
import com.alibaba.polardb.index.topology.ShardTopologyListener;
import com.alibaba.polardb.index.zookeeper.ZkPositionManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Service;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import io.micrometer.core.instrument.Timer;

@Service
public class IndexRebalanceService {

    private static final Logger logger = LoggerFactory.getLogger(IndexRebalanceService.class);

    @Autowired
    private GlobalIndexDao globalIndexDao;

    @Autowired
    private ShardTopologyListener topologyListener;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    private ZkPositionManager zkPositionManager;

    @Autowired(required = false)
    private SyncMetrics syncMetrics;

    private final AtomicBoolean rebalanceInProgress = new AtomicBoolean(false);
    private final AtomicBoolean stopRequested = new AtomicBoolean(false);

    private final AtomicLong totalRecordsToMigrate = new AtomicLong(0);
    private final AtomicLong migratedRecords = new AtomicLong(0);
    private final AtomicLong failedRecords = new AtomicLong(0);
    private final AtomicLong startTime = new AtomicLong(0);

    private ExecutorService rebalanceExecutor;
    private volatile long currentRebalanceVersion = -1;

    @PostConstruct
    public void init() {
        RebalanceConfig config = properties.getRebalance();
        int threads = config != null ? config.getThreads() : 8;
        rebalanceExecutor = new ThreadPoolExecutor(
                threads, threads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<>(10000),
                r -> {
                    Thread t = new Thread(r, "rebalance-" + r.hashCode());
                    t.setDaemon(true);
                    return t;
                },
                new ThreadPoolExecutor.CallerRunsPolicy()
        );
        logger.info("IndexRebalanceService initialized with {} threads", threads);
    }

    public synchronized void startRebalance(Set<String> addedShards, Set<String> removedShards, long topologyVersion) {
        startRebalance(addedShards, removedShards, null, topologyVersion);
    }

    @Async("processorExecutor")
    public synchronized void startRebalance(Set<String> addedShards, Set<String> removedShards,
                                            Set<String> specificShards, long topologyVersion) {
        if (!rebalanceInProgress.compareAndSet(false, true)) {
            logger.warn("Rebalance already in progress");
            return;
        }

        Timer.Sample timerSample = null;
        try {
            stopRequested.set(false);
            currentRebalanceVersion = topologyVersion;
            startTime.set(System.currentTimeMillis());
            migratedRecords.set(0);
            failedRecords.set(0);
            totalRecordsToMigrate.set(0);

            if (syncMetrics != null) {
                syncMetrics.setRebalanceInProgress(true);
                syncMetrics.setTopologyVersion(topologyVersion);
                syncMetrics.setDualReadModeActive(true);
                timerSample = syncMetrics.startRebalanceTimer();
            }

            RebalanceConfig config = properties.getRebalance();
            if (config != null && config.isDryRun()) {
                logger.info("Dry run mode enabled, skipping actual migration");
                long estimated = estimateRecordsToMigrate();
                logger.info("Estimated records to migrate: {}", estimated);
                return;
            }

            logger.info("Starting index rebalance, version: {}, added shards: {}, removed shards: {}",
                    topologyVersion, addedShards, removedShards);

            saveRebalanceMarker(topologyVersion);

            if (specificShards != null && !specificShards.isEmpty()) {
                logger.info("Rebalancing specific shards: {}", specificShards);
                rebalanceSpecificShards(specificShards);
            } else {
                rebalanceAllShards();
            }

            verifyAndSwitch(topologyVersion);

        } catch (Exception e) {
            logger.error("Rebalance failed, version: {}", topologyVersion, e);
            if (syncMetrics != null) {
                syncMetrics.recordRebalanceFailed(failedRecords.get());
            }
            throw new RuntimeException("Rebalance failed", e);
        } finally {
            rebalanceInProgress.set(false);
            clearRebalanceMarker();
            long duration = System.currentTimeMillis() - startTime.get();

            if (syncMetrics != null) {
                syncMetrics.setRebalanceInProgress(false);
                syncMetrics.setRebalanceRecordsMigrated(migratedRecords.get());
                syncMetrics.setDualReadModeActive(false);
                syncMetrics.stopRebalanceTimer(timerSample);
            }

            logger.info("Rebalance finished, version: {}, duration: {}ms, migrated: {}, failed: {}",
                    topologyVersion, duration, migratedRecords.get(), failedRecords.get());
        }
    }

    private void rebalanceAllShards() throws Exception {
        ConsistentHash<String> hash = topologyListener.getConsistentHash();
        if (hash == null) {
            throw new IllegalStateException("Consistent hash not initialized");
        }

        long totalCount = globalIndexDao.count();
        totalRecordsToMigrate.set(totalCount);
        if (syncMetrics != null) {
            syncMetrics.setRebalanceRecordsToMigrate(totalCount);
            syncMetrics.setRebalanceRecordsMigrated(0);
        }
        logger.info("Total records to check for migration: {}", totalCount);

        RebalanceConfig config = properties.getRebalance();
        int batchSize = config != null ? config.getBatchSize() : 500;
        int maxRetries = config != null ? config.getMaxRetryTimes() : 3;

        String lastGlobalId = null;
        long processedCount = 0;

        while (!stopRequested.get() && processedCount < totalCount) {
            try {
                List<GlobalIndex> batch = globalIndexDao.findByGlobalIdRange(lastGlobalId, null, batchSize);
                if (batch.isEmpty()) {
                    break;
                }

                List<Future<Integer>> futures = new ArrayList<>();
                for (GlobalIndex record : batch) {
                    futures.add(rebalanceExecutor.submit(() -> {
                        try {
                            return migrateRecordIfNeeded(record, hash);
                        } catch (Exception e) {
                            logger.error("Error migrating record: {}", record.getGlobalId(), e);
                            failedRecords.incrementAndGet();
                            return 0;
                        }
                    }));
                }

                for (Future<Integer> future : futures) {
                    try {
                        future.get(config.getRetryIntervalMs() * 10, TimeUnit.MILLISECONDS);
                    } catch (Exception e) {
                        logger.error("Error waiting for migration task", e);
                        failedRecords.incrementAndGet();
                    }
                }

                processedCount += batch.size();
                lastGlobalId = batch.get(batch.size() - 1).getGlobalId();

                if (processedCount % 10000 == 0) {
                    logger.info("Rebalance progress: {}/{}, migrated: {}",
                            processedCount, totalCount, migratedRecords.get());
                }

            } catch (Exception e) {
                logger.error("Error processing batch at lastGlobalId: {}", lastGlobalId, e);
                if (maxRetries > 0) {
                    maxRetries--;
                    Thread.sleep(config.getRetryIntervalMs());
                    continue;
                }
                throw e;
            }
        }
    }

    private void rebalanceSpecificShards(Set<String> shardIds) throws Exception {
        ConsistentHash<String> hash = topologyListener.getConsistentHash();
        if (hash == null) {
            throw new IllegalStateException("Consistent hash not initialized");
        }

        RebalanceConfig config = properties.getRebalance();
        int batchSize = config != null ? config.getBatchSize() : 500;

        long totalToCheck = 0;
        for (String shardId : shardIds) {
            totalToCheck += globalIndexDao.countByShardId(shardId);
        }
        totalRecordsToMigrate.set(totalToCheck);
        logger.info("Total records to check for specific shards {}: {}", shardIds, totalToCheck);

        for (String shardId : shardIds) {
            if (stopRequested.get()) {
                break;
            }

            logger.info("Rebalancing shard: {}", shardId);
            long shardCount = globalIndexDao.countByShardId(shardId);
            long processed = 0;
            String lastGlobalId = null;

            while (!stopRequested.get() && processed < shardCount) {
                List<GlobalIndex> batch = queryByShardIdWithPagination(shardId, lastGlobalId, batchSize);
                if (batch.isEmpty()) {
                    break;
                }

                List<Map<String, String>> updates = new ArrayList<>();
                for (GlobalIndex record : batch) {
                    String expectedShardId = hash.get(record.getGlobalId());
                    if (expectedShardId != null && !expectedShardId.equals(record.getShardId())) {
                        Map<String, String> update = new HashMap<>();
                        update.put("globalId", record.getGlobalId());
                        update.put("newShardId", expectedShardId);
                        updates.add(update);
                    }
                }

                if (!updates.isEmpty()) {
                    int updated = globalIndexDao.batchUpdateShardId(updates);
                    migratedRecords.addAndGet(updated);
                    if (syncMetrics != null) {
                        for (Map<String, String> update : updates) {
                            syncMetrics.recordSyncSuccess(update.get("newShardId"));
                        }
                    }
                }

                processed += batch.size();
                lastGlobalId = batch.get(batch.size() - 1).getGlobalId();

                if (processed % 5000 == 0) {
                    logger.info("Shard {} rebalance progress: {}/{}, total migrated: {}",
                            shardId, processed, shardCount, migratedRecords.get());
                }
            }
        }
    }

    private List<GlobalIndex> queryByShardIdWithPagination(String shardId, String lastGlobalId, int limit) {
        List<GlobalIndex> all = globalIndexDao.findByShardId(shardId);
        if (all.isEmpty()) {
            return Collections.emptyList();
        }

        List<GlobalIndex> result = new ArrayList<>();
        boolean started = (lastGlobalId == null);
        for (GlobalIndex index : all) {
            if (started) {
                result.add(index);
                if (result.size() >= limit) {
                    break;
                }
            } else if (index.getGlobalId().equals(lastGlobalId)) {
                started = true;
            }
        }
        return result;
    }

    private Integer migrateRecordIfNeeded(GlobalIndex record, ConsistentHash<String> hash) {
        String expectedShardId = hash.get(record.getGlobalId());
        if (expectedShardId == null || expectedShardId.equals(record.getShardId())) {
            return 0;
        }

        try {
            Map<String, String> update = new HashMap<>();
            update.put("globalId", record.getGlobalId());
            update.put("newShardId", expectedShardId);
            globalIndexDao.batchUpdateShardId(Collections.singletonList(update));
            migratedRecords.incrementAndGet();
            if (syncMetrics != null) {
                syncMetrics.recordSyncSuccess(expectedShardId);
                syncMetrics.recordRebalanceMigrated(1);
                syncMetrics.setRebalanceRecordsMigrated(migratedRecords.get());
            }
            logger.debug("Migrated globalId: {} from {} to {}",
                    record.getGlobalId(), record.getShardId(), expectedShardId);
            return 1;
        } catch (Exception e) {
            logger.warn("Failed to migrate globalId: {}", record.getGlobalId(), e);
            failedRecords.incrementAndGet();
            if (syncMetrics != null) {
                syncMetrics.recordSyncFailed(record.getShardId());
                syncMetrics.recordRebalanceFailed(1);
            }
            return 0;
        }
    }

    private long estimateRecordsToMigrate() {
        ConsistentHash<String> hash = topologyListener.getConsistentHash();
        if (hash == null) {
            return 0;
        }

        long estimated = 0;
        List<String> shardIds = globalIndexDao.findAllShardIds();

        for (String shardId : shardIds) {
            long shardCount = globalIndexDao.countByShardId(shardId);
            Set<String> nodes = hash.getNodes();

            if (!nodes.contains(shardId)) {
                estimated += shardCount;
            } else {
                double migrationRatio = estimateMigrationRatio(shardId, nodes.size());
                estimated += (long) (shardCount * migrationRatio);
            }
        }

        return estimated;
    }

    private double estimateMigrationRatio(String shardId, int totalNodes) {
        if (totalNodes <= 1) {
            return 0.0;
        }
        return 1.0 / totalNodes;
    }

    private void verifyAndSwitch(long topologyVersion) throws Exception {
        logger.info("Verifying rebalance results, version: {}", topologyVersion);

        ConsistentHash<String> hash = topologyListener.getConsistentHash();
        if (hash == null) {
            throw new IllegalStateException("Consistent hash not initialized");
        }

        long verifyCount = 0;
        long mismatchCount = 0;
        int sampleSize = 1000;

        List<GlobalIndex> sample = globalIndexDao.findAllWithPagination(0, sampleSize);
        for (GlobalIndex record : sample) {
            verifyCount++;
            String expectedShardId = hash.get(record.getGlobalId());
            if (expectedShardId != null && !expectedShardId.equals(record.getShardId())) {
                mismatchCount++;
                logger.warn("Mismatch found: globalId={}, expected={}, actual={}",
                        record.getGlobalId(), expectedShardId, record.getShardId());
            }
        }

        double mismatchRate = verifyCount > 0 ? (double) mismatchCount / verifyCount : 0;
        logger.info("Verification complete: checked={}, mismatched={}, mismatchRate={}%",
                verifyCount, mismatchCount, String.format("%.4f", mismatchRate * 100));

        if (mismatchRate > 0.01) {
            logger.warn("Mismatch rate too high ({}%), re-switching to dual read mode",
                    String.format("%.2f", mismatchRate * 100));
            return;
        }

        atomicSwitch(topologyVersion);
    }

    private void atomicSwitch(long topologyVersion) {
        logger.info("Performing atomic switch to new topology, version: {}", topologyVersion);

        zkPositionManager.savePosition("TOPOLOGY_VERSION",
                com.alibaba.polardb.index.model.BinlogPosition.builder()
                        .journalName("TOPOLOGY_SWITCH")
                        .position(topologyVersion)
                        .timestamp(System.currentTimeMillis())
                        .serverId("rebalance")
                        .build());

        topologyListener.disableDualReadMode();

        logger.info("Atomic switch completed successfully, version: {}", topologyVersion);
    }

    private void saveRebalanceMarker(long topologyVersion) {
        try {
            String path = "/rebalance/running";
            Map<String, Object> data = new HashMap<>();
            data.put("version", topologyVersion);
            data.put("startTime", System.currentTimeMillis());
            zkPositionManager.savePosition("REBALANCE_RUNNING",
                    com.alibaba.polardb.index.model.BinlogPosition.builder()
                            .journalName("REBALANCE")
                            .position(topologyVersion)
                            .timestamp(System.currentTimeMillis())
                            .serverId("rebalance")
                            .build());
        } catch (Exception e) {
            logger.warn("Failed to save rebalance marker", e);
        }
    }

    private void clearRebalanceMarker() {
        try {
            zkPositionManager.clearPosition("REBALANCE_RUNNING");
        } catch (Exception e) {
            logger.warn("Failed to clear rebalance marker", e);
        }
    }

    public boolean isRebalanceInProgress() {
        return rebalanceInProgress.get();
    }

    public void stopRebalance() {
        stopRequested.set(true);
        logger.info("Rebalance stop requested");
    }

    public Map<String, Object> getRebalanceStatus() {
        Map<String, Object> status = new LinkedHashMap<>();
        status.put("inProgress", rebalanceInProgress.get());
        status.put("version", currentRebalanceVersion);
        status.put("totalRecordsToMigrate", totalRecordsToMigrate.get());
        status.put("migratedRecords", migratedRecords.get());
        status.put("failedRecords", failedRecords.get());
        status.put("startTime", startTime.get() > 0 ? new Date(startTime.get()) : null);

        if (startTime.get() > 0 && totalRecordsToMigrate.get() > 0) {
            long elapsed = System.currentTimeMillis() - startTime.get();
            long migrated = migratedRecords.get();
            double progress = (double) migrated / totalRecordsToMigrate.get() * 100;
            long remaining = migrated > 0 ? (elapsed * (totalRecordsToMigrate.get() - migrated) / migrated) : 0;

            status.put("progressPercent", String.format("%.2f%%", progress));
            status.put("elapsedMs", elapsed);
            status.put("estimatedRemainingMs", remaining);
            status.put("throughput", migrated > 0 ? (migrated * 1000.0 / elapsed) : 0);
        }

        return status;
    }

    public void setRebalanceThreads(int threads) {
        if (threads <= 0) {
            throw new IllegalArgumentException("Thread count must be positive");
        }

        if (rebalanceInProgress.get()) {
            throw new IllegalStateException("Cannot change thread count during rebalance");
        }

        shutdownExecutor();

        rebalanceExecutor = new ThreadPoolExecutor(
                threads, threads, 0L, TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<>(10000),
                r -> {
                    Thread t = new Thread(r, "rebalance-" + r.hashCode());
                    t.setDaemon(true);
                    return t;
                },
                new ThreadPoolExecutor.CallerRunsPolicy()
        );

        logger.info("Rebalance thread pool size updated to {}", threads);
    }

    private void shutdownExecutor() {
        if (rebalanceExecutor != null) {
            rebalanceExecutor.shutdown();
            try {
                if (!rebalanceExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                    rebalanceExecutor.shutdownNow();
                }
            } catch (InterruptedException e) {
                rebalanceExecutor.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }
    }

    public boolean triggerFullRebalance() {
        if (rebalanceInProgress.get()) {
            return false;
        }

        Set<String> allShards = topologyListener.getCurrentShardIds();
        long version = topologyListener.getTopologyVersion() + 1;
        startRebalance(Collections.emptySet(), Collections.emptySet(), allShards, version);
        return true;
    }

    public boolean triggerShardRebalance(String shardId) {
        if (rebalanceInProgress.get()) {
            return false;
        }

        Set<String> shards = Collections.singleton(shardId);
        long version = topologyListener.getTopologyVersion() + 1;
        startRebalance(Collections.emptySet(), Collections.emptySet(), shards, version);
        return true;
    }

    @PreDestroy
    public void destroy() {
        stopRequested.set(true);
        shutdownExecutor();
        clearRebalanceMarker();
        logger.info("IndexRebalanceService destroyed");
    }
}
