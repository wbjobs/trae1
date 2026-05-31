package com.alibaba.polardb.index.controller;

import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.canal.MultiShardCanalListener;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.hash.ConsistentHash;
import com.alibaba.polardb.index.join.cache.QueryCache;
import com.alibaba.polardb.index.join.executor.QueryExecutor;
import com.alibaba.polardb.index.join.model.*;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.alibaba.polardb.index.model.ShardInfo;
import com.alibaba.polardb.index.monitor.SyncMetrics;
import com.alibaba.polardb.index.rebalance.IndexRebalanceService;
import com.alibaba.polardb.index.rebuild.FullRebuildService;
import com.alibaba.polardb.index.router.DualReadRouter;
import com.alibaba.polardb.index.topology.ShardTopologyListener;
import com.alibaba.polardb.index.zookeeper.ZkPositionManager;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.*;

@RestController
@RequestMapping("/api/v1/global-index")
public class GlobalIndexController {

    @Autowired
    private GlobalIndexDao globalIndexDao;

    @Autowired
    private ZkPositionManager zkPositionManager;

    @Autowired
    private MultiShardCanalListener canalListener;

    @Autowired
    private FullRebuildService rebuildService;

    @Autowired
    private SyncMetrics syncMetrics;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired(required = false)
    private DualReadRouter dualReadRouter;

    @Autowired(required = false)
    private IndexRebalanceService rebalanceService;

    @Autowired(required = false)
    private ShardTopologyListener topologyListener;

    @Autowired(required = false)
    private QueryExecutor queryExecutor;

    @Autowired(required = false)
    private QueryCache queryCache;

    @GetMapping("/query/{globalId}")
    public ResponseEntity<Map<String, Object>> queryByGlobalId(@PathVariable String globalId) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        boolean useDualRead = topologyListener != null && topologyListener.isDualReadMode();
        result.put("dualReadMode", useDualRead);

        GlobalIndex index;
        if (useDualRead && dualReadRouter != null) {
            index = dualReadRouter.queryWithDualRead(globalId);
        } else {
            index = globalIndexDao.findByGlobalId(globalId);
        }

        if (index != null) {
            result.put("found", true);
            Map<String, Object> data = new HashMap<>();
            data.put("globalId", index.getGlobalId());
            data.put("shardKey", index.getShardKey());
            data.put("shardId", index.getShardId());
            data.put("gmtCreate", index.getGmtCreate());
            data.put("gmtModified", index.getGmtModified());
            result.put("data", data);

            if (topologyListener != null) {
                String expectedShardId = topologyListener.getShardIdByGlobalId(globalId);
                result.put("expectedShardId", expectedShardId);
                result.put("consistentHashMatch", Objects.equals(expectedShardId, index.getShardId()));
            }
        } else {
            result.put("found", false);
            result.put("message", "Global ID not found: " + globalId);
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/query/batch")
    public ResponseEntity<Map<String, Object>> queryByGlobalIds(@RequestParam List<String> globalIds) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        boolean useDualRead = topologyListener != null && topologyListener.isDualReadMode();
        result.put("dualReadMode", useDualRead);

        List<GlobalIndex> indices;
        if (useDualRead && dualReadRouter != null) {
            indices = dualReadRouter.batchQueryWithDualRead(globalIds);
        } else {
            indices = new ArrayList<>();
            for (String globalId : globalIds) {
                GlobalIndex index = globalIndexDao.findByGlobalId(globalId);
                if (index != null) {
                    indices.add(index);
                }
            }
        }

        List<Map<String, Object>> data = new ArrayList<>();
        Set<String> foundIds = new HashSet<>();

        for (GlobalIndex index : indices) {
            Map<String, Object> item = new HashMap<>();
            item.put("globalId", index.getGlobalId());
            item.put("shardKey", index.getShardKey());
            item.put("shardId", index.getShardId());
            data.add(item);
            foundIds.add(index.getGlobalId());
        }

        List<String> notFound = new ArrayList<>();
        for (String globalId : globalIds) {
            if (!foundIds.contains(globalId)) {
                notFound.add(globalId);
            }
        }

        result.put("foundCount", data.size());
        result.put("notFoundCount", notFound.size());
        result.put("data", data);
        if (!notFound.isEmpty()) {
            result.put("notFound", notFound);
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/shard/{shardKey}")
    public ResponseEntity<Map<String, Object>> queryByShardKey(@PathVariable String shardKey) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        List<GlobalIndex> indices = globalIndexDao.findByShardKey(shardKey);
        List<Map<String, Object>> data = new ArrayList<>();
        for (GlobalIndex index : indices) {
            Map<String, Object> item = new HashMap<>();
            item.put("globalId", index.getGlobalId());
            item.put("shardKey", index.getShardKey());
            item.put("shardId", index.getShardId());
            data.add(item);
        }

        result.put("count", data.size());
        result.put("data", data);
        return ResponseEntity.ok(result);
    }

    @PostMapping("/rebuild")
    public ResponseEntity<Map<String, Object>> triggerRebuild(@RequestParam(required = false) String shardId) {
        Map<String, Object> result = new HashMap<>();
        result.put("timestamp", System.currentTimeMillis());

        if (rebuildService.isRebuildInProgress()) {
            result.put("success", false);
            result.put("message", "Rebuild already in progress");
            return ResponseEntity.status(409).body(result);
        }

        boolean success;
        if (shardId != null && !shardId.isEmpty()) {
            success = rebuildService.rebuildShard(shardId);
            result.put("operation", "rebuild-shard");
            result.put("shardId", shardId);
        } else {
            success = rebuildService.rebuildAll();
            result.put("operation", "rebuild-all");
        }

        result.put("success", success);
        result.put("message", success ? "Rebuild started successfully" : "Rebuild failed");

        return ResponseEntity.ok(result);
    }

    @GetMapping("/status")
    public ResponseEntity<Map<String, Object>> getStatus() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        Map<String, Object> syncStatus = new HashMap<>();
        syncStatus.put("rebuildInProgress", rebuildService.isRebuildInProgress());
        syncStatus.put("canalListeners", canalListener.getListenerStatus());

        List<ShardInfo> shardInfoList = new ArrayList<>();
        for (ShardConfig shardConfig : properties.getShards()) {
            String shardId = shardConfig.getId();
            ShardInfo info = ShardInfo.builder()
                    .shardId(shardId)
                    .shardName(shardConfig.getName())
                    .status(canalListener.getListenerStatus().getOrDefault(shardId, false) ? 1 : 0)
                    .totalRows(globalIndexDao.countByShardId(shardId))
                    .syncRows(syncMetrics.getSyncSuccessCount(shardId))
                    .delayMs(syncMetrics.getSyncDelayMs(shardId))
                    .build();
            shardInfoList.add(info);
        }
        syncStatus.put("shards", shardInfoList);

        Map<String, Object> metrics = new HashMap<>();
        metrics.put("totalSyncSuccess", syncMetrics.getTotalSyncSuccess());
        metrics.put("totalSyncFailed", syncMetrics.getTotalSyncFailed());
        metrics.put("totalIndexCount", globalIndexDao.count());

        result.put("syncStatus", syncStatus);
        result.put("metrics", metrics);

        return ResponseEntity.ok(result);
    }

    @PostMapping("/listener/{shardId}/start")
    public ResponseEntity<Map<String, Object>> startListener(@PathVariable String shardId) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("operation", "start-listener");
        result.put("shardId", shardId);

        canalListener.startListener(shardId);
        result.put("message", "Listener started for shard: " + shardId);

        return ResponseEntity.ok(result);
    }

    @PostMapping("/listener/{shardId}/stop")
    public ResponseEntity<Map<String, Object>> stopListener(@PathVariable String shardId) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("operation", "stop-listener");
        result.put("shardId", shardId);

        canalListener.stopListener(shardId);
        result.put("message", "Listener stopped for shard: " + shardId);

        return ResponseEntity.ok(result);
    }

    @GetMapping("/position/{shardId}")
    public ResponseEntity<Map<String, Object>> getPosition(@PathVariable String shardId) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("shardId", shardId);

        com.alibaba.polardb.index.model.BinlogPosition position = zkPositionManager.getPosition(shardId);
        if (position != null) {
            Map<String, Object> posData = new HashMap<>();
            posData.put("journalName", position.getJournalName());
            posData.put("position", position.getPosition());
            posData.put("timestamp", position.getTimestamp());
            posData.put("serverId", position.getServerId());
            result.put("position", posData);
        } else {
            result.put("position", null);
            result.put("message", "No position found for shard: " + shardId);
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/rebalance")
    public ResponseEntity<Map<String, Object>> triggerRebalance(
            @RequestParam(required = false) String shardId,
            @RequestParam(required = false, defaultValue = "false") boolean dryRun) {
        Map<String, Object> result = new HashMap<>();
        result.put("timestamp", System.currentTimeMillis());

        if (rebalanceService == null) {
            result.put("success", false);
            result.put("message", "Rebalance service not available");
            return ResponseEntity.status(503).body(result);
        }

        if (rebalanceService.isRebalanceInProgress()) {
            result.put("success", false);
            result.put("message", "Rebalance already in progress");
            return ResponseEntity.status(409).body(result);
        }

        if (topologyListener != null && topologyListener.isRebalanceInProgress()) {
            result.put("success", false);
            result.put("message", "Topology change in progress");
            return ResponseEntity.status(409).body(result);
        }

        boolean success;
        if (dryRun) {
            result.put("dryRun", true);
            result.put("estimatedRecords", 0);
            success = true;
        } else if (shardId != null && !shardId.isEmpty()) {
            success = rebalanceService.triggerShardRebalance(shardId);
            result.put("operation", "rebalance-shard");
            result.put("shardId", shardId);
        } else {
            success = rebalanceService.triggerFullRebalance();
            result.put("operation", "rebalance-all");
        }

        result.put("success", success);
        result.put("message", success ? "Rebalance started successfully" : "Rebalance failed to start");

        return ResponseEntity.ok(result);
    }

    @GetMapping("/rebalance/status")
    public ResponseEntity<Map<String, Object>> getRebalanceStatus() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (rebalanceService != null) {
            result.putAll(rebalanceService.getRebalanceStatus());
        } else {
            result.put("inProgress", false);
            result.put("message", "Rebalance service not available");
        }

        if (topologyListener != null) {
            result.put("dualReadMode", topologyListener.isDualReadMode());
            result.put("topologyVersion", topologyListener.getTopologyVersion());
            result.put("topologyInProgress", topologyListener.isRebalanceInProgress());
            result.put("currentShards", topologyListener.getCurrentShardIds());
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/rebalance/stop")
    public ResponseEntity<Map<String, Object>> stopRebalance() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("operation", "stop-rebalance");

        if (rebalanceService != null) {
            rebalanceService.stopRebalance();
            result.put("message", "Rebalance stop requested");
        } else {
            result.put("success", false);
            result.put("message", "Rebalance service not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/rebalance/threads")
    public ResponseEntity<Map<String, Object>> setRebalanceThreads(@RequestParam int threads) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (rebalanceService != null) {
            try {
                rebalanceService.setRebalanceThreads(threads);
                result.put("threads", threads);
                result.put("message", "Rebalance thread pool size updated to " + threads);
            } catch (Exception e) {
                result.put("success", false);
                result.put("message", e.getMessage());
                return ResponseEntity.status(400).body(result);
            }
        } else {
            result.put("success", false);
            result.put("message", "Rebalance service not available");
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/topology")
    public ResponseEntity<Map<String, Object>> getTopologyInfo() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (topologyListener != null) {
            result.put("topologyVersion", topologyListener.getTopologyVersion());
            result.put("dualReadMode", topologyListener.isDualReadMode());
            result.put("rebalanceInProgress", topologyListener.isRebalanceInProgress());
            result.put("currentShards", topologyListener.getCurrentShardIds());
            result.put("lastCheckTime", topologyListener.getLastTopologyCheckTime());

            ConsistentHash<String> hash = topologyListener.getConsistentHash();
            if (hash != null) {
                result.put("consistentHashNodes", hash.getNodes());
                result.put("virtualNodeCount", hash.size());
            }

            Map<String, Object> shardConfigs = new HashMap<>();
            for (Map.Entry<String, ShardConfig> entry : topologyListener.getAllShardConfigs().entrySet()) {
                ShardConfig config = entry.getValue();
                Map<String, Object> shardInfo = new HashMap<>();
                shardInfo.put("id", config.getId());
                shardInfo.put("name", config.getName());
                shardInfo.put("status", config.getStatus());
                shardInfo.put("hashWeight", config.getHashWeight());
                shardInfo.put("shardKey", config.getShardKey());
                shardInfo.put("globalIdColumn", config.getGlobalIdColumn());
                ConsistentHash.HashRange range = hash != null ? hash.getNodeHashRange(config.getHashKey()) : null;
                if (range != null) {
                    shardInfo.put("hashRangeStart", range.getStart());
                    shardInfo.put("hashRangeEnd", range.getEnd());
                }
                shardConfigs.put(entry.getKey(), shardInfo);
            }
            result.put("shardConfigs", shardConfigs);
        } else {
            result.put("message", "Topology listener not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/topology/refresh")
    public ResponseEntity<Map<String, Object>> refreshTopology() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (topologyListener != null) {
            topologyListener.initializeTopology();
            if (dualReadRouter != null) {
                dualReadRouter.refreshShardConnections();
            }
            result.put("message", "Topology refreshed");
            result.put("currentShards", topologyListener.getCurrentShardIds());
        } else {
            result.put("success", false);
            result.put("message", "Topology listener not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/dual-read/enable")
    public ResponseEntity<Map<String, Object>> enableDualRead() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("operation", "enable-dual-read");

        if (topologyListener != null) {
            if (properties.getRebalance() == null || properties.getRebalance().isEnableDualRead()) {
                topologyListener.initializeTopology();
                result.put("message", "Dual read mode enabled");
            } else {
                result.put("success", false);
                result.put("message", "Dual read is disabled in configuration");
            }
        } else {
            result.put("success", false);
            result.put("message", "Topology listener not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/dual-read/disable")
    public ResponseEntity<Map<String, Object>> disableDualRead() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("operation", "disable-dual-read");

        if (topologyListener != null) {
            topologyListener.disableDualReadMode();
            result.put("message", "Dual read mode disabled");
        } else {
            result.put("success", false);
            result.put("message", "Topology listener not available");
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/hash/calculate")
    public ResponseEntity<Map<String, Object>> calculateHash(@RequestParam String globalId) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());
        result.put("globalId", globalId);

        if (topologyListener != null) {
            ConsistentHash<String> hash = topologyListener.getConsistentHash();
            if (hash != null) {
                long hashValue = hash.getHash(globalId);
                String mappedShard = hash.get(globalId);
                result.put("hashValue", hashValue);
                result.put("mappedShardId", mappedShard);

                GlobalIndex currentIndex = globalIndexDao.findByGlobalId(globalId);
                if (currentIndex != null) {
                    result.put("currentShardId", currentIndex.getShardId());
                    result.put("needsMigration", !Objects.equals(mappedShard, currentIndex.getShardId()));
                }
            }
        } else {
            result.put("message", "Topology listener not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/join/execute")
    public ResponseEntity<Map<String, Object>> executeJoin(
            @RequestBody JoinQuery joinQuery,
            @RequestParam(required = false, defaultValue = "false") boolean explain,
            @RequestParam(required = false) Long timeoutMs) {

        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (queryExecutor == null) {
            result.put("success", false);
            result.put("message", "Join query executor not available");
            return ResponseEntity.status(503).body(result);
        }

        try {
            if (properties.getJoin() != null && !properties.getJoin().isEnabled()) {
                result.put("success", false);
                result.put("message", "Cross-shard join is disabled in configuration");
                return ResponseEntity.status(403).body(result);
            }

            joinQuery.setExplain(explain);
            if (timeoutMs != null && timeoutMs > 0) {
                joinQuery.setTimeoutMs(timeoutMs);
            }

            JoinResult joinResult = queryExecutor.execute(joinQuery);
            result.put("queryId", joinResult.getQueryId());
            result.put("fromCache", joinResult.isFromCache());
            result.put("executionTimeMs", joinResult.getExecutionTimeMs());
            result.put("joinTimeMs", joinResult.getJoinTimeMs());
            result.put("shardCount", joinResult.getShardCount());
            result.put("shardQueryTimes", joinResult.getShardQueryTimes());
            result.put("totalRows", joinResult.getTotalRows());

            if (explain && joinResult.getExecutionPlan() != null) {
                result.put("explain", joinResult.getExecutionPlan().toExplainString());
                result.put("plan", joinResult.getExecutionPlan());
            }

            if (joinResult.isSuccess()) {
                result.put("data", joinResult.getRows());
            } else {
                result.put("success", false);
                result.put("errorMessage", joinResult.getErrorMessage());
            }

        } catch (Exception e) {
            logger.error("Failed to execute join query: {}", e.getMessage(), e);
            result.put("success", false);
            result.put("errorMessage", e.getMessage());
            return ResponseEntity.status(500).body(result);
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/join/explain")
    public ResponseEntity<Map<String, Object>> explainJoin(@RequestBody JoinQuery joinQuery) {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (queryExecutor == null) {
            result.put("success", false);
            result.put("message", "Join query executor not available");
            return ResponseEntity.status(503).body(result);
        }

        try {
            joinQuery.setExplain(true);
            JoinResult joinResult = queryExecutor.execute(joinQuery);

            result.put("queryId", joinResult.getQueryId());
            result.put("explain", joinResult.getRows().get(0).get("explain"));

            if (joinResult.getExecutionPlan() != null) {
                QueryPlan plan = joinResult.getExecutionPlan();
                Map<String, Object> planInfo = new HashMap<>();
                planInfo.put("planId", plan.getPlanId());
                planInfo.put("joinStrategy", plan.getJoinStrategy());
                planInfo.put("joinAlgorithm", plan.getJoinAlgorithm());
                planInfo.put("estimatedCost", plan.getEstimatedCost());
                planInfo.put("estimatedRows", plan.getEstimatedRows());
                planInfo.put("estimatedTimeMs", plan.getEstimatedTimeMs());
                planInfo.put("usePredicatePushdown", plan.isUsePredicatePushdown());
                planInfo.put("useResultCache", plan.isUseResultCache());
                planInfo.put("steps", plan.getSteps());
                planInfo.put("shardQueries", plan.getShardQueries());
                planInfo.put("shardEstimatedRows", plan.getShardEstimatedRows());
                result.put("plan", planInfo);
            }

        } catch (Exception e) {
            logger.error("Failed to explain join query: {}", e.getMessage(), e);
            result.put("success", false);
            result.put("errorMessage", e.getMessage());
            return ResponseEntity.status(500).body(result);
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/join/cache/status")
    public ResponseEntity<Map<String, Object>> getCacheStatus() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (queryCache != null) {
            Map<String, Object> cacheInfo = new HashMap<>();
            cacheInfo.put("enabled", queryCache.isCacheEnabled());
            cacheInfo.put("size", queryCache.getSize());
            cacheInfo.put("hitCount", queryCache.getHitCount());
            cacheInfo.put("missCount", queryCache.getMissCount());
            cacheInfo.put("hitRate", String.format("%.2f%%", queryCache.getHitRate() * 100));

            if (queryCache.getStats() != null) {
                cacheInfo.put("evictionCount", queryCache.getStats().evictionCount());
                cacheInfo.put("loadCount", queryCache.getStats().loadCount());
                cacheInfo.put("averageLoadPenaltyMs", queryCache.getStats().averageLoadPenalty() / 1000000.0);
            }

            result.put("cache", cacheInfo);
        } else {
            result.put("message", "Query cache not available");
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/join/cache/invalidate")
    public ResponseEntity<Map<String, Object>> invalidateCache(
            @RequestBody(required = false) JoinQuery query) {

        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (queryCache != null) {
            if (query != null) {
                queryCache.invalidate(query);
                result.put("operation", "invalidate-query");
            } else {
                queryCache.invalidateAll();
                result.put("operation", "invalidate-all");
            }
            result.put("message", "Cache invalidated successfully");
        } else {
            result.put("success", false);
            result.put("message", "Query cache not available");
        }

        return ResponseEntity.ok(result);
    }

    @GetMapping("/join/stats")
    public ResponseEntity<Map<String, Object>> getJoinStats() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        if (properties.getJoin() != null) {
            Map<String, Object> config = new HashMap<>();
            config.put("enabled", properties.getJoin().isEnabled());
            config.put("timeoutMs", properties.getJoin().getTimeoutMs());
            config.put("executorThreads", properties.getJoin().getExecutorThreads());
            config.put("predicatePushdownEnabled", properties.getJoin().isPredicatePushdownEnabled());
            config.put("resultCacheEnabled", properties.getJoin().isResultCacheEnabled());
            config.put("hashJoinThreshold", properties.getJoin().getHashJoinThreshold());
            config.put("defaultJoinAlgorithm", properties.getJoin().getDefaultJoinAlgorithm());
            result.put("config", config);
        }

        if (queryCache != null) {
            Map<String, Object> cache = new HashMap<>();
            cache.put("hitCount", queryCache.getHitCount());
            cache.put("missCount", queryCache.getMissCount());
            cache.put("hitRate", queryCache.getHitRate());
            result.put("cache", cache);
        }

        return ResponseEntity.ok(result);
    }

    @PostMapping("/join/example")
    public ResponseEntity<Map<String, Object>> getExampleQuery() {
        Map<String, Object> result = new HashMap<>();
        result.put("success", true);
        result.put("timestamp", System.currentTimeMillis());

        JoinQuery exampleQuery = JoinQuery.builder()
                .queryId("example-001")
                .timeoutMs(30000L)
                .explain(false)
                .build();

        JoinTable userTable = JoinTable.builder()
                .tableName("t_user")
                .alias("u")
                .schema("shard_db")
                .globalIdColumn("user_id")
                .shardKeyColumn("user_id")
                .selectColumns(Arrays.asList("user_id", "user_name", "create_time"))
                .predicates(Arrays.asList(
                        Predicate.builder()
                                .column("status")
                                .operator("EQ")
                                .value(1)
                                .tableAlias("u")
                                .pushdownSupported(true)
                                .build()
                ))
                .build();

        JoinTable orderTable = JoinTable.builder()
                .tableName("t_order")
                .alias("o")
                .schema("shard_db")
                .globalIdColumn("order_id")
                .shardKeyColumn("user_id")
                .selectColumns(Arrays.asList("order_id", "user_id", "amount", "order_time"))
                .predicates(Arrays.asList(
                        Predicate.builder()
                                .column("order_status")
                                .operator("EQ")
                                .value("PAID")
                                .tableAlias("o")
                                .pushdownSupported(true)
                                .build(),
                        Predicate.builder()
                                .column("amount")
                                .operator("GTE")
                                .value(100)
                                .tableAlias("o")
                                .pushdownSupported(true)
                                .build()
                ))
                .build();

        JoinCondition joinCondition = JoinCondition.builder()
                .leftTable("u")
                .leftColumn("user_id")
                .rightTable("o")
                .rightColumn("user_id")
                .operator("=")
                .joinType(JoinCondition.JoinType.INNER)
                .build();

        exampleQuery.addTable(userTable);
        exampleQuery.addTable(orderTable);
        exampleQuery.addJoinCondition(joinCondition);

        result.put("description", "Example: Join user and order tables by user_id across shards");
        result.put("query", exampleQuery);
        result.put("curlExample", "curl -X POST http://localhost:8080/api/v1/global-index/join/execute \\\n" +
                "  -H 'Content-Type: application/json' \\\n" +
                "  -d '{\"tables\": [...], \"joinConditions\": [...]}'");
        result.put("curlExplainExample", "curl -X POST 'http://localhost:8080/api/v1/global-index/join/execute?explain=true' \\\n" +
                "  -H 'Content-Type: application/json' \\\n" +
                "  -d '{\"tables\": [...], \"joinConditions\": [...]}'");

        return ResponseEntity.ok(result);
    }

    private static final org.slf4j.Logger logger =
            org.slf4j.LoggerFactory.getLogger(GlobalIndexController.class);
}
