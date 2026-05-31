package com.logservice.storage;

import com.logservice.codec.BlockCodec;
import com.logservice.config.ServiceConfig;
import com.logservice.model.LineMetadata;
import com.github.luben.zstd.Zstd;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.ReentrantLock;

public class BlockManager {
    private static final Logger LOG = LoggerFactory.getLogger(BlockManager.class);

    private final ServiceConfig config;
    private final IndexManager indexManager;
    private final AggregationManager aggregationManager;
    private final Path dataDir;
    private final ReentrantLock lock = new ReentrantLock();

    private final AtomicLong blockIdSeq = new AtomicLong(1L);
    private final AtomicLong lineIdSeq = new AtomicLong(1L);

    private volatile ActiveBlock active;
    private volatile long lastFlushTimeMs;

    public BlockManager(ServiceConfig config, IndexManager indexManager) throws IOException {
        this(config, indexManager, null);
    }

    public BlockManager(ServiceConfig config, IndexManager indexManager,
                         AggregationManager aggregationManager) throws IOException {
        this.config = config;
        this.indexManager = indexManager;
        this.aggregationManager = aggregationManager;
        this.dataDir = Paths.get(config.getDataDir());
        Files.createDirectories(dataDir);
        this.lastFlushTimeMs = System.currentTimeMillis();
        this.active = new ActiveBlock(blockIdSeq.getAndIncrement());
    }

    public void append(List<String> lines) {
        if (lines == null || lines.isEmpty()) return;
        lock.lock();
        try {
            long now = System.currentTimeMillis();
            for (String line : lines) {
                appendInternal(line);
                if (aggregationManager != null) {
                    aggregationManager.recordLine(line, now);
                }
            }
            checkFlushBySize();
        } finally {
            lock.unlock();
        }
    }

    public void tickFlush() {
        lock.lock();
        try {
            if (active == null || active.offsets.isEmpty()) return;
            long now = System.currentTimeMillis();
            if (now - lastFlushTimeMs >= config.getBlockFlushIntervalMs()) {
                flushActiveInternal("time");
            }
        } finally {
            lock.unlock();
        }
    }

    public void flushNow() {
        lock.lock();
        try {
            if (active != null && !active.offsets.isEmpty()) {
                flushActiveInternal("manual");
            }
        } finally {
            lock.unlock();
        }
    }

    private void appendInternal(String line) {
        if (line == null) line = "";
        byte[] bytes = line.getBytes(StandardCharsets.UTF_8);
        int offset = active.rawBuffer.size();
        active.rawBuffer.write(bytes, 0, bytes.length);
        active.offsets.add(offset);
        active.lengths.add(bytes.length);
        active.uncompressedSize += bytes.length;
        long lineId = lineIdSeq.getAndIncrement();
        active.lineIds.add(lineId);
        active.lines.add(line);
    }

    private void checkFlushBySize() {
        if (active != null && active.uncompressedSize >= config.getBlockSizeBytes()) {
            flushActiveInternal("size");
        }
    }

    private void flushActiveInternal(String reason) {
        if (active == null || active.offsets.isEmpty()) return;
        long blockId = active.blockId;
        long timestamp = System.currentTimeMillis();
        try {
            byte[] rawBytes = active.rawBuffer.toByteArray();
            byte[] compressed = Zstd.compress(rawBytes, config.getZstdCompressionLevel());
            if (Zstd.isError(compressed.length)) {
                throw new IOException("Zstd compress error: " + Zstd.getErrorName(compressed.length));
            }

            String fileName = String.format("block-%012d-%d.logblk", blockId, timestamp);
            File file = dataDir.resolve(fileName).toFile();
            try (RandomAccessFile raf = new RandomAccessFile(file, "rw")) {
                BlockCodec.writeBlock(raf, blockId, timestamp, compressed, new ArrayList<>(active.offsets));
            }

            List<LineMetadata> metas = new ArrayList<>(active.offsets.size());
            int totalRaw = rawBytes.length;
            for (int i = 0; i < active.offsets.size(); i++) {
                int off = active.offsets.get(i);
                int len = (i + 1 < active.offsets.size())
                        ? (active.offsets.get(i + 1) - off)
                        : (totalRaw - off);
                metas.add(new LineMetadata(active.lineIds.get(i), blockId, off, len, timestamp));
            }

            indexManager.indexBlock(metas, active.lines, blockId, file.getName());

            LOG.info("Flushed block {} ({} lines, {}B raw, {}B compressed, reason={})",
                    blockId, metas.size(), totalRaw, compressed.length, reason);
        } catch (IOException e) {
            LOG.error("Failed to flush block {}", blockId, e);
        } finally {
            this.active = new ActiveBlock(blockIdSeq.getAndIncrement());
            this.lastFlushTimeMs = System.currentTimeMillis();
        }
    }

    public Path getDataDir() { return dataDir; }

    public long getCurrentLineId() { return lineIdSeq.get(); }
    public long getCurrentBlockId() { return active == null ? 0 : active.blockId; }

    private static class ActiveBlock {
        final long blockId;
        final ByteArrayOutputStream rawBuffer = new ByteArrayOutputStream(1024 * 1024);
        final List<Integer> offsets = new ArrayList<>(2048);
        final List<Integer> lengths = new ArrayList<>(2048);
        final List<Long> lineIds = new ArrayList<>(2048);
        final List<String> lines = new ArrayList<>(2048);
        int uncompressedSize = 0;

        ActiveBlock(long blockId) { this.blockId = blockId; }
    }
}
