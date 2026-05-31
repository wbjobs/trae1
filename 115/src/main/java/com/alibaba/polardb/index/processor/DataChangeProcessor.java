package com.alibaba.polardb.index.processor;

import com.alibaba.otter.canal.protocol.CanalEntry;
import com.alibaba.polardb.index.config.GlobalIndexSyncProperties;
import com.alibaba.polardb.index.config.ProcessorConfig;
import com.alibaba.polardb.index.dao.GlobalIndexDao;
import com.alibaba.polardb.index.model.GlobalIndex;
import com.alibaba.polardb.index.model.event.DataChangeEvent;
import com.alibaba.polardb.index.monitor.SyncMetrics;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import javax.annotation.PreDestroy;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;

@Component
public class DataChangeProcessor {

    private static final Logger logger = LoggerFactory.getLogger(DataChangeProcessor.class);

    @Autowired
    private GlobalIndexDao globalIndexDao;

    @Autowired
    private GlobalIndexSyncProperties properties;

    @Autowired
    private SyncMetrics syncMetrics;

    private BlockingQueue<DataChangeEvent> eventQueue;
    private final AtomicBoolean running = new AtomicBoolean(false);
    private Thread flushThread;
    private ScheduledExecutorService scheduledExecutor;

    @PostConstruct
    public void init() {
        ProcessorConfig config = properties.getProcessor();
        eventQueue = new LinkedBlockingQueue<>(config.getBatchQueueCapacity());

        running.set(true);
        flushThread = new Thread(this::runFlushLoop, "index-flush-thread");
        flushThread.setDaemon(true);
        flushThread.start();

        scheduledExecutor = Executors.newSingleThreadScheduledExecutor();
        scheduledExecutor.scheduleAtFixedRate(() -> {
            try {
                long count = globalIndexDao.count();
                syncMetrics.updateTotalIndexCount(count);
            } catch (Exception e) {
                logger.warn("Failed to update total index count metric", e);
            }
        }, 5, 30, TimeUnit.SECONDS);

        logger.info("DataChangeProcessor initialized, queue capacity: {}, flush interval: {}ms",
                config.getBatchQueueCapacity(), config.getFlushIntervalMs());
    }

    @Async("processorExecutor")
    public void process(DataChangeEvent event) {
        if (event == null || event.getGlobalId() == null) {
            return;
        }

        long startTime = System.currentTimeMillis();

        try {
            CanalEntry.EventType eventType = event.getEventType();
            switch (eventType) {
                case INSERT:
                    handleInsert(event);
                    break;
                case UPDATE:
                    handleUpdate(event);
                    break;
                case DELETE:
                    handleDelete(event);
                    break;
                default:
                    logger.debug("Skip unsupported event type: {} for globalId: {}",
                            eventType, event.getGlobalId());
                    break;
            }

            long duration = System.currentTimeMillis() - startTime;
            syncMetrics.recordProcessTime(event.getShardId(), duration);

        } catch (Exception e) {
            logger.error("Failed to process event, shard: {}, globalId: {}, eventType: {}",
                    event.getShardId(), event.getGlobalId(), event.getEventType(), e);
            throw new RuntimeException("Process event failed", e);
        }
    }

    private void handleInsert(DataChangeEvent event) {
        GlobalIndex index = buildGlobalIndex(event);
        globalIndexDao.upsert(index);
        logger.debug("Insert global index, globalId: {}, shardId: {}, shardKey: {}",
                event.getGlobalId(), event.getShardId(), event.getShardKey());
    }

    private void handleUpdate(DataChangeEvent event) {
        GlobalIndex index = buildGlobalIndex(event);
        globalIndexDao.upsert(index);
        logger.debug("Update global index, globalId: {}, shardId: {}, shardKey: {}",
                event.getGlobalId(), event.getShardId(), event.getShardKey());
    }

    private void handleDelete(DataChangeEvent event) {
        globalIndexDao.deleteByGlobalId(event.getGlobalId());
        logger.debug("Delete global index, globalId: {}", event.getGlobalId());
    }

    public void processBatch(List<DataChangeEvent> events) {
        if (events == null || events.isEmpty()) {
            return;
        }

        List<GlobalIndex> upsertList = new ArrayList<>();
        List<String> deleteList = new ArrayList<>();
        Date now = new Date();

        for (DataChangeEvent event : events) {
            if (event == null || event.getGlobalId() == null) {
                continue;
            }

            CanalEntry.EventType eventType = event.getEventType();
            if (eventType == CanalEntry.EventType.DELETE) {
                deleteList.add(event.getGlobalId());
            } else {
                GlobalIndex index = buildGlobalIndex(event, now);
                upsertList.add(index);
            }
        }

        if (!upsertList.isEmpty()) {
            globalIndexDao.batchUpsert(upsertList);
            logger.debug("Batch upsert {} global index records", upsertList.size());
        }

        if (!deleteList.isEmpty()) {
            for (String globalId : deleteList) {
                globalIndexDao.deleteByGlobalId(globalId);
            }
            logger.debug("Batch delete {} global index records", deleteList.size());
        }
    }

    private GlobalIndex buildGlobalIndex(DataChangeEvent event) {
        return buildGlobalIndex(event, new Date());
    }

    private GlobalIndex buildGlobalIndex(DataChangeEvent event, Date now) {
        return GlobalIndex.builder()
                .globalId(event.getGlobalId())
                .shardKey(event.getShardKey())
                .shardId(event.getShardId())
                .gmtCreate(now)
                .gmtModified(now)
                .build();
    }

    private void runFlushLoop() {
        ProcessorConfig config = properties.getProcessor();
        long flushInterval = config.getFlushIntervalMs();
        int maxBatchSize = config.getMaxBatchSize();

        while (running.get()) {
            try {
                List<DataChangeEvent> batch = new ArrayList<>();
                long startTime = System.currentTimeMillis();

                while (batch.size() < maxBatchSize &&
                        (System.currentTimeMillis() - startTime) < flushInterval) {
                    DataChangeEvent event = eventQueue.poll(10, TimeUnit.MILLISECONDS);
                    if (event != null) {
                        batch.add(event);
                    } else if (batch.isEmpty()) {
                        break;
                    }
                }

                if (!batch.isEmpty()) {
                    processBatch(batch);
                }

            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            } catch (Exception e) {
                logger.error("Error in flush loop", e);
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException ie) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
        }
        logger.info("Flush loop stopped");
    }

    public boolean enqueueEvent(DataChangeEvent event) {
        try {
            return eventQueue.offer(event, 100, TimeUnit.MILLISECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    public int getQueueSize() {
        return eventQueue.size();
    }

    @PreDestroy
    public void destroy() {
        running.set(false);

        if (scheduledExecutor != null) {
            scheduledExecutor.shutdown();
            try {
                if (!scheduledExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                    scheduledExecutor.shutdownNow();
                }
            } catch (InterruptedException e) {
                scheduledExecutor.shutdownNow();
            }
        }

        if (flushThread != null) {
            flushThread.interrupt();
            try {
                flushThread.join(5000);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }

        if (!eventQueue.isEmpty()) {
            logger.warn("Event queue has {} remaining events on shutdown", eventQueue.size());
        }

        logger.info("DataChangeProcessor destroyed");
    }
}
