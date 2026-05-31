package com.alibaba.polardb.index.monitor;

import io.micrometer.core.instrument.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

@Component
public class SyncMetrics {

    @Autowired
    private MeterRegistry meterRegistry;

    private final ConcurrentHashMap<String, AtomicLong> syncSuccessCounters = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, AtomicLong> syncFailedCounters = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, AtomicLong> syncDelayGauges = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Timer> syncTimers = new ConcurrentHashMap<>();

    private Counter totalSyncSuccess;
    private Counter totalSyncFailed;
    private Gauge totalIndexCount;
    private final AtomicLong totalIndexCountValue = new AtomicLong(0);

    private Counter rebalanceMigratedTotal;
    private Counter rebalanceFailedTotal;
    private final AtomicLong rebalanceInProgress = new AtomicLong(0);
    private final AtomicLong rebalanceRecordsToMigrate = new AtomicLong(0);
    private final AtomicLong rebalanceRecordsMigrated = new AtomicLong(0);
    private final AtomicLong dualReadModeActive = new AtomicLong(0);
    private final AtomicLong topologyVersion = new AtomicLong(0);
    private Timer rebalanceDurationTimer;

    @PostConstruct
    public void init() {
        totalSyncSuccess = Counter.builder("global_index_sync_success_total")
                .description("Total number of successful sync events")
                .register(meterRegistry);

        totalSyncFailed = Counter.builder("global_index_sync_failed_total")
                .description("Total number of failed sync events")
                .register(meterRegistry);

        totalIndexCount = Gauge.builder("global_index_total_count", totalIndexCountValue, AtomicLong::get)
                .description("Total number of records in global index table")
                .register(meterRegistry);

        Gauge.builder("global_index_tps_estimate", this, SyncMetrics::getEstimatedTps)
                .description("Estimated TPS of sync process")
                .register(meterRegistry);

        rebalanceMigratedTotal = Counter.builder("global_index_rebalance_migrated_total")
                .description("Total number of records migrated during rebalance")
                .register(meterRegistry);

        rebalanceFailedTotal = Counter.builder("global_index_rebalance_failed_total")
                .description("Total number of records failed during rebalance")
                .register(meterRegistry);

        Gauge.builder("global_index_rebalance_in_progress", rebalanceInProgress, AtomicLong::get)
                .description("Whether rebalance is in progress (1=yes, 0=no)")
                .register(meterRegistry);

        Gauge.builder("global_index_rebalance_records_to_migrate", rebalanceRecordsToMigrate, AtomicLong::get)
                .description("Total number of records to migrate in current rebalance")
                .register(meterRegistry);

        Gauge.builder("global_index_rebalance_records_migrated", rebalanceRecordsMigrated, AtomicLong::get)
                .description("Number of records migrated in current rebalance")
                .register(meterRegistry);

        Gauge.builder("global_index_dual_read_mode_active", dualReadModeActive, AtomicLong::get)
                .description("Whether dual read mode is active (1=yes, 0=no)")
                .register(meterRegistry);

        Gauge.builder("global_index_topology_version", topologyVersion, AtomicLong::get)
                .description("Current topology version")
                .register(meterRegistry);

        rebalanceDurationTimer = Timer.builder("global_index_rebalance_duration_seconds")
                .description("Duration of rebalance operations")
                .register(meterRegistry);
    }

    public void recordSyncSuccess(String shardId) {
        AtomicLong counter = syncSuccessCounters.computeIfAbsent(shardId, k -> {
            AtomicLong value = new AtomicLong(0);
            Gauge.builder("global_index_sync_success", value, AtomicLong::get)
                    .tag("shard", shardId)
                    .description("Number of successful sync events for shard")
                    .register(meterRegistry);
            return value;
        });
        counter.incrementAndGet();
        totalSyncSuccess.increment();
    }

    public void recordSyncFailed(String shardId) {
        AtomicLong counter = syncFailedCounters.computeIfAbsent(shardId, k -> {
            AtomicLong value = new AtomicLong(0);
            Gauge.builder("global_index_sync_failed", value, AtomicLong::get)
                    .tag("shard", shardId)
                    .description("Number of failed sync events for shard")
                    .register(meterRegistry);
            return value;
        });
        counter.incrementAndGet();
        totalSyncFailed.increment();
    }

    public void recordSyncDelay(String shardId, long delayMs) {
        AtomicLong gauge = syncDelayGauges.computeIfAbsent(shardId, k -> {
            AtomicLong value = new AtomicLong(0);
            Gauge.builder("global_index_sync_delay_ms", value, AtomicLong::get)
                    .tag("shard", shardId)
                    .description("Sync delay in milliseconds for shard")
                    .register(meterRegistry);
            return value;
        });
        gauge.set(delayMs);
    }

    public void recordProcessTime(String shardId, long durationMs) {
        Timer timer = syncTimers.computeIfAbsent(shardId, k ->
                Timer.builder("global_index_process_duration_ms")
                        .tag("shard", shardId)
                        .description("Processing duration for sync events")
                        .register(meterRegistry)
        );
        timer.record(durationMs, TimeUnit.MILLISECONDS);
    }

    public void updateTotalIndexCount(long count) {
        totalIndexCountValue.set(count);
    }

    private double getEstimatedTps() {
        long totalSuccess = 0;
        for (AtomicLong value : syncSuccessCounters.values()) {
            totalSuccess += value.get();
        }
        return totalSuccess / (System.currentTimeMillis() / 1000.0 + 1) * 1000;
    }

    public long getSyncSuccessCount(String shardId) {
        AtomicLong counter = syncSuccessCounters.get(shardId);
        return counter != null ? counter.get() : 0;
    }

    public long getSyncFailedCount(String shardId) {
        AtomicLong counter = syncFailedCounters.get(shardId);
        return counter != null ? counter.get() : 0;
    }

    public long getSyncDelayMs(String shardId) {
        AtomicLong gauge = syncDelayGauges.get(shardId);
        return gauge != null ? gauge.get() : 0;
    }

    public long getTotalSyncSuccess() {
        long total = 0;
        for (AtomicLong value : syncSuccessCounters.values()) {
            total += value.get();
        }
        return total;
    }

    public long getTotalSyncFailed() {
        long total = 0;
        for (AtomicLong value : syncFailedCounters.values()) {
            total += value.get();
        }
        return total;
    }

    public void recordRebalanceMigrated(long count) {
        if (rebalanceMigratedTotal != null) {
            rebalanceMigratedTotal.increment(count);
        }
        rebalanceRecordsMigrated.addAndGet(count);
    }

    public void recordRebalanceFailed(long count) {
        if (rebalanceFailedTotal != null) {
            rebalanceFailedTotal.increment(count);
        }
    }

    public void setRebalanceInProgress(boolean inProgress) {
        rebalanceInProgress.set(inProgress ? 1 : 0);
    }

    public void setRebalanceRecordsToMigrate(long count) {
        rebalanceRecordsToMigrate.set(count);
    }

    public void setRebalanceRecordsMigrated(long count) {
        rebalanceRecordsMigrated.set(count);
    }

    public void setDualReadModeActive(boolean active) {
        dualReadModeActive.set(active ? 1 : 0);
    }

    public void setTopologyVersion(long version) {
        topologyVersion.set(version);
    }

    public Timer.Sample startRebalanceTimer() {
        if (rebalanceDurationTimer != null) {
            return Timer.start(meterRegistry);
        }
        return null;
    }

    public void stopRebalanceTimer(Timer.Sample sample) {
        if (sample != null && rebalanceDurationTimer != null) {
            sample.stop(rebalanceDurationTimer);
        }
    }

    public long getRebalanceRecordsMigrated() {
        return rebalanceRecordsMigrated.get();
    }

    public long getRebalanceRecordsToMigrate() {
        return rebalanceRecordsToMigrate.get();
    }
}
