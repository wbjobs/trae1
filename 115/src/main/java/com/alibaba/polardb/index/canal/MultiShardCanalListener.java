package com.alibaba.polardb.index.canal;

import com.alibaba.otter.canal.protocol.CanalEntry;
import com.alibaba.otter.canal.protocol.Message;
import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ShardConfig;
import com.alibaba.polardb.index.model.BinlogPosition;
import com.alibaba.polardb.index.model.event.DataChangeEvent;
import com.alibaba.polardb.index.monitor.SyncMetrics;
import com.alibaba.polardb.index.processor.DataChangeProcessor;
import com.alibaba.polardb.index.topology.ShardTopologyListener;
import com.alibaba.polardb.index.zookeeper.ZkPositionManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Lazy;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;

@Component
public class MultiShardCanalListener {

    private static final Logger logger = LoggerFactory.getLogger(MultiShardCanalListener.class);

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    private ZkPositionManager zkPositionManager;

    @Autowired
    private DataChangeProcessor dataChangeProcessor;

    @Autowired
    private SyncMetrics syncMetrics;

    @Autowired(required = false)
    @Lazy
    private ShardTopologyListener topologyListener;

    private final Map<String, CanalConnector> connectorMap = new ConcurrentHashMap<>();
    private final Map<String, BinlogEventParser> parserMap = new ConcurrentHashMap<>();
    private final Map<String, AtomicBoolean> runningMap = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        for (ShardConfig shardConfig : properties.getShards()) {
            CanalConnector connector = new CanalConnector(shardConfig);
            BinlogEventParser parser = new BinlogEventParser(shardConfig);

            if (topologyListener != null) {
                parser.setDdlEventListener((shardId, eventType, sql) -> {
                    try {
                        topologyListener.onDdlEvent(shardId, eventType, sql);
                    } catch (Exception e) {
                        logger.warn("Error handling DDL event for shard: {}", shardId, e);
                    }
                });
            }

            connectorMap.put(shardConfig.getId(), connector);
            parserMap.put(shardConfig.getId(), parser);
            runningMap.put(shardConfig.getId(), new AtomicBoolean(false));
        }
        logger.info("MultiShardCanalListener initialized with {} shards", connectorMap.size());
    }

    public void startAllListeners() {
        for (ShardConfig shardConfig : properties.getShards()) {
            startListener(shardConfig.getId());
        }
    }

    @Async("canalListenerExecutor")
    public void startListener(String shardId) {
        ShardConfig shardConfig = findShardConfig(shardId);
        if (shardConfig == null) {
            logger.error("Shard config not found for shard: {}", shardId);
            return;
        }

        AtomicBoolean running = runningMap.get(shardId);
        if (!running.compareAndSet(false, true)) {
            logger.warn("Listener for shard {} is already running", shardId);
            return;
        }

        CanalConnector connector = connectorMap.get(shardId);
        BinlogEventParser parser = parserMap.get(shardId);

        try {
            connector.connect();
            connector.subscribe(".*\\..*");
            connector.rollback();

            BinlogPosition position = zkPositionManager.getPosition(shardId);
            if (position != null) {
                logger.info("Resume from position for shard {}: {}", shardId, position);
            } else {
                logger.info("Starting from latest position for shard {}", shardId);
            }

            logger.info("Canal listener started for shard: {}", shardId);

            while (running.get()) {
                try {
                    Message message = connector.getWithoutAck(shardConfig.getBatchSize());
                    long batchId = message.getId();
                    int size = message.getEntries().size();

                    if (batchId == -1 || size == 0) {
                        try {
                            Thread.sleep(100);
                        } catch (InterruptedException e) {
                            Thread.currentThread().interrupt();
                            break;
                        }
                        continue;
                    }

                    long receiveTime = System.currentTimeMillis();
                    long lastTimestamp = 0;
                    String lastJournal = null;
                    long lastPosition = 0;
                    boolean processSuccess = true;

                    for (CanalEntry.Entry entry : message.getEntries()) {
                        try {
                            List<DataChangeEvent> events = parser.parse(entry, receiveTime);
                            if (!events.isEmpty()) {
                                for (DataChangeEvent event : events) {
                                    try {
                                        dataChangeProcessor.process(event);
                                        syncMetrics.recordSyncSuccess(shardId);
                                        long delayMs = receiveTime - event.getExecuteTime();
                                        syncMetrics.recordSyncDelay(shardId, delayMs);
                                    } catch (Exception e) {
                                        logger.error("Failed to process event for shard {}, globalId: {}",
                                                shardId, event.getGlobalId(), e);
                                        syncMetrics.recordSyncFailed(shardId);
                                        processSuccess = false;
                                    }
                                }
                            }
                            lastTimestamp = entry.getHeader().getExecuteTime();
                            lastJournal = entry.getHeader().getLogfileName();
                            lastPosition = entry.getHeader().getLogfileOffset();
                        } catch (Exception e) {
                            logger.error("Parse entry error for shard: {}", shardId, e);
                            processSuccess = false;
                        }
                    }

                    if (processSuccess && lastJournal != null) {
                        BinlogPosition newPosition = BinlogPosition.builder()
                                .journalName(lastJournal)
                                .position(lastPosition)
                                .timestamp(lastTimestamp)
                                .serverId(String.valueOf(message.getId()))
                                .build();
                        zkPositionManager.savePosition(shardId, newPosition);
                        connector.ack(batchId);
                    } else {
                        connector.rollback(batchId);
                        logger.warn("Rollback batch {} for shard {}", batchId, shardId);
                    }

                } catch (Exception e) {
                    logger.error("Error processing message for shard: {}", shardId, e);
                    try {
                        Thread.sleep(3000);
                    } catch (InterruptedException ie) {
                        Thread.currentThread().interrupt();
                        break;
                    }
                }
            }

        } catch (Exception e) {
            logger.error("Canal listener error for shard: {}", shardId, e);
        } finally {
            running.set(false);
            connector.disconnect();
            logger.info("Canal listener stopped for shard: {}", shardId);
        }
    }

    public void stopListener(String shardId) {
        AtomicBoolean running = runningMap.get(shardId);
        if (running != null) {
            running.set(false);
        }
        CanalConnector connector = connectorMap.get(shardId);
        if (connector != null) {
            connector.disconnect();
        }
        logger.info("Stopped listener for shard: {}", shardId);
    }

    public void stopAllListeners() {
        for (String shardId : connectorMap.keySet()) {
            stopListener(shardId);
        }
    }

    public Map<String, Boolean> getListenerStatus() {
        Map<String, Boolean> status = new HashMap<>();
        for (Map.Entry<String, AtomicBoolean> entry : runningMap.entrySet()) {
            status.put(entry.getKey(), entry.getValue().get());
        }
        return status;
    }

    private ShardConfig findShardConfig(String shardId) {
        for (ShardConfig config : properties.getShards()) {
            if (config.getId().equals(shardId)) {
                return config;
            }
        }
        return null;
    }

    @PreDestroy
    public void destroy() {
        stopAllListeners();
        logger.info("MultiShardCanalListener destroyed");
    }
}
