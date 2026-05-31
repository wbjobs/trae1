package com.logservice;

import com.logservice.config.ServiceConfig;
import com.logservice.http.HttpServer;
import com.logservice.storage.AggregationManager;
import com.logservice.storage.BlockManager;
import com.logservice.storage.IndexManager;
import com.logservice.storage.RetentionManager;
import com.logservice.storage.SearchService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class Main {
    private static final Logger LOG = LoggerFactory.getLogger(Main.class);

    public static void main(String[] args) throws Exception {
        ServiceConfig config = new ServiceConfig();
        applyOverrides(config, args);

        IndexManager indexManager = new IndexManager();
        AggregationManager aggregationManager = new AggregationManager(config);
        BlockManager blockManager = new BlockManager(config, indexManager, aggregationManager);
        SearchService searchService = new SearchService(indexManager, blockManager, config);
        RetentionManager retentionManager = new RetentionManager(blockManager, indexManager,
                searchService, config.getRetentionDays());

        ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(3, r -> {
            Thread t = new Thread(r, "logservice-scheduler");
            t.setDaemon(true);
            return t;
        });

        scheduler.scheduleAtFixedRate(blockManager::tickFlush,
                config.getBlockFlushIntervalMs(), config.getBlockFlushIntervalMs(), TimeUnit.MILLISECONDS);
        scheduler.scheduleAtFixedRate(retentionManager::runOnce,
                config.getRetentionCheckIntervalMs(), config.getRetentionCheckIntervalMs(), TimeUnit.MILLISECONDS);
        scheduler.scheduleAtFixedRate(aggregationManager::rollOver,
                config.getAggregationRollIntervalMs(), config.getAggregationRollIntervalMs(), TimeUnit.MILLISECONDS);

        HttpServer http = new HttpServer(config, blockManager, indexManager, searchService, aggregationManager);
        http.start();

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            LOG.info("Shutting down Log Compression Index Service");
            try {
                blockManager.flushNow();
                aggregationManager.rollOver();
            } catch (Exception ignored) {}
            http.shutdown();
            scheduler.shutdownNow();
        }, "logservice-shutdown"));

        LOG.info("Log Compression Index Service started on port {}, data dir: {}",
                config.getHttpPort(), config.getDataDir());
    }

    private static void applyOverrides(ServiceConfig config, String[] args) {
        for (int i = 0; i < args.length - 1; i += 2) {
            String key = args[i];
            String val = args[i + 1];
            switch (key) {
                case "--port": config.setHttpPort(Integer.parseInt(val)); break;
                case "--dataDir": config.setDataDir(val); break;
                case "--blockSize": config.setBlockSizeBytes(Integer.parseInt(val)); break;
                case "--flushMs": config.setBlockFlushIntervalMs(Long.parseLong(val)); break;
                case "--zstdLevel": config.setZstdCompressionLevel(Integer.parseInt(val)); break;
                case "--retentionDays": config.setRetentionDays(Long.parseLong(val)); break;
                case "--searchTimeoutMs": config.setSearchTimeoutMs(Integer.parseInt(val)); break;
                case "--aggTimeoutMs": config.setAggregationTimeoutMs(Integer.parseInt(val)); break;
                default: break;
            }
        }
    }
}
