package com.logservice.storage;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.time.Instant;
import java.util.Map;

public class RetentionManager {
    private static final Logger LOG = LoggerFactory.getLogger(RetentionManager.class);

    private final BlockManager blockManager;
    private final IndexManager indexManager;
    private final SearchService searchService;
    private final long retentionDays;

    public RetentionManager(BlockManager blockManager, IndexManager indexManager,
                             SearchService searchService, long retentionDays) {
        this.blockManager = blockManager;
        this.indexManager = indexManager;
        this.searchService = searchService;
        this.retentionDays = retentionDays;
    }

    public void runOnce() {
        if (retentionDays <= 0) return;
        Instant cutoff = Instant.now().minus(Duration.ofDays(retentionDays));
        for (Map.Entry<Long, Long> e : indexManager.blocks()) {
            long blockId = e.getKey();
            long ts = e.getValue();
            Instant blockTime = Instant.ofEpochMilli(ts);
            if (blockTime.isBefore(cutoff)) {
                deleteBlock(blockId);
            }
        }
    }

    private void deleteBlock(long blockId) {
        String fileName = indexManager.getBlockFileName(blockId);
        if (fileName != null) {
            Path path = blockManager.getDataDir().resolve(fileName);
            try {
                Files.deleteIfExists(path);
                LOG.info("Deleted expired block file: {}", path);
            } catch (IOException ex) {
                LOG.error("Failed to delete expired block file: {}", path, ex);
            }
        }
        indexManager.dropBlock(blockId);
        searchService.invalidateBlock(blockId);
        LOG.info("Dropped expired block {} from index", blockId);
    }
}
