package com.logservice.storage;

import com.logservice.codec.BlockCodec;
import com.logservice.config.ServiceConfig;
import com.logservice.model.LineMetadata;
import com.logservice.model.SearchHit;
import com.logservice.model.SearchResult;
import com.github.luben.zstd.Zstd;
import org.roaringbitmap.longlong.Roaring64NavigableMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

public class SearchService {
    private static final Logger LOG = LoggerFactory.getLogger(SearchService.class);
    private static final int TIMEOUT_CHECK_EVERY = 256;
    private static final int MAX_PAGE_SIZE = 100;

    private final IndexManager indexManager;
    private final BlockManager blockManager;
    private final ServiceConfig config;

    private final ConcurrentHashMap<Long, String[]> blockLineCache = new ConcurrentHashMap<>();
    private final Deque<Long> cacheLru = new ArrayDeque<>();
    private final Object cacheLock = new Object();

    public SearchService(IndexManager indexManager, BlockManager blockManager, ServiceConfig config) {
        this.indexManager = indexManager;
        this.blockManager = blockManager;
        this.config = config;
    }

    public SearchResult search(String query, long cursor, int limit) {
        long startNs = System.nanoTime();
        long timeoutNs = config.getSearchTimeoutMs() * 1_000_000L;
        int pageSize = Math.max(1, Math.min(limit, MAX_PAGE_SIZE));

        String[] tokens = indexManager.tokenizePhrase(query);
        if (tokens.length == 0) {
            return new SearchResult(Collections.emptyList(), false, null,
                    false, null, elapsedMs(startNs));
        }

        Roaring64NavigableMap[] bitmaps = new Roaring64NavigableMap[tokens.length];
        int smallestIdx = 0;
        long smallestCardinality = Long.MAX_VALUE;
        for (int i = 0; i < tokens.length; i++) {
            bitmaps[i] = indexManager.getTermBitmap(tokens[i]);
            if (bitmaps[i] == null) {
                return new SearchResult(Collections.emptyList(), false, null,
                        false, null, elapsedMs(startNs));
            }
            long card = bitmaps[i].getLongCardinality();
            if (card < smallestCardinality) {
                smallestCardinality = card;
                smallestIdx = i;
            }
        }

        List<Long> matchedIds = new ArrayList<>(pageSize);
        boolean truncated = false;
        boolean hasMore = false;
        Long nextCursor = null;

        var iterator = bitmaps[smallestIdx].getLongIterator();
        long scanned = 0;
        while (iterator.hasNext()) {
            if (scanned % TIMEOUT_CHECK_EVERY == 0) {
                if (System.nanoTime() - startNs > timeoutNs) {
                    truncated = true;
                    break;
                }
            }
            scanned++;
            long lineId = iterator.next();
            if (cursor > 0 && lineId <= cursor) continue;

            if (tokens.length > 1) {
                boolean allMatch = true;
                for (int i = 0; i < tokens.length; i++) {
                    if (i == smallestIdx) continue;
                    if (!bitmaps[i].contains(lineId)) {
                        allMatch = false;
                        break;
                    }
                }
                if (!allMatch) continue;
            }

            matchedIds.add(lineId);
            if (matchedIds.size() >= pageSize) {
                if (iterator.hasNext()) {
                    hasMore = true;
                    nextCursor = lineId;
                }
                break;
            }
        }

        if (matchedIds.isEmpty()) {
            return new SearchResult(Collections.emptyList(), hasMore, nextCursor,
                    truncated, truncated ? "结果不完整，请缩小搜索范围" : null, elapsedMs(startNs));
        }

        Map<Long, String[]> blockLines = loadBlocksFor(matchedIds);
        if (blockLines.isEmpty()) {
            return new SearchResult(Collections.emptyList(), hasMore, nextCursor,
                    truncated, truncated ? "结果不完整，请缩小搜索范围" : null, elapsedMs(startNs));
        }

        int ctx = config.getSearchContextLines();
        String lowerQuery = query.toLowerCase();
        List<SearchHit> hits = new ArrayList<>(matchedIds.size());
        for (Long lineId : matchedIds) {
            LineMetadata meta = indexManager.getLineMeta(lineId);
            if (meta == null) continue;
            String[] lines = blockLines.get(meta.getBlockId());
            if (lines == null) continue;
            int pos = findLineIndex(lines, meta.getOffsetInBlock(), meta.getLength());
            if (pos < 0) continue;
            if (!lines[pos].toLowerCase().contains(lowerQuery)) continue;

            String[] before = new String[Math.min(ctx, pos)];
            for (int i = 0; i < before.length; i++) before[i] = lines[pos - before.length + i];
            int afterCount = Math.min(ctx, lines.length - 1 - pos);
            String[] after = new String[afterCount];
            for (int i = 0; i < afterCount; i++) after[i] = lines[pos + 1 + i];

            hits.add(new SearchHit(lineId, meta.getTimestamp(), lines[pos], before, after));
        }

        return new SearchResult(hits, hasMore, nextCursor,
                truncated, truncated ? "结果不完整，请缩小搜索范围" : null, elapsedMs(startNs));
    }

    private Map<Long, String[]> loadBlocksFor(List<Long> matchedIds) {
        Set<Long> neededBlockIds = new HashSet<>();
        for (Long id : matchedIds) {
            LineMetadata meta = indexManager.getLineMeta(id);
            if (meta != null) neededBlockIds.add(meta.getBlockId());
        }
        Map<Long, String[]> blockLines = new HashMap<>();
        for (Long bid : neededBlockIds) {
            String[] lines = getBlockLines(bid);
            if (lines != null) blockLines.put(bid, lines);
        }
        return blockLines;
    }

    private int findLineIndex(String[] lines, int offset, int length) {
        int runningOffset = 0;
        for (int i = 0; i < lines.length; i++) {
            int lineLen = lines[i].getBytes(StandardCharsets.UTF_8).length;
            if (runningOffset == offset && lineLen == length) return i;
            runningOffset += lineLen;
        }
        return -1;
    }

    String[] getBlockLines(long blockId) {
        String[] cached = blockLineCache.get(blockId);
        if (cached != null) {
            synchronized (cacheLock) {
                cacheLru.remove(blockId);
                cacheLru.addLast(blockId);
            }
            return cached;
        }
        String fileName = indexManager.getBlockFileName(blockId);
        if (fileName == null) return null;
        Path path = blockManager.getDataDir().resolve(fileName);
        if (!java.nio.file.Files.exists(path)) return null;
        try (RandomAccessFile raf = new RandomAccessFile(path.toFile(), "r")) {
            BlockCodec.BlockInfo info = BlockCodec.readBlock(raf);
            if (info == null) return null;
            int totalRaw = (int) Zstd.decompressedSize(info.compressedPayload);
            if (totalRaw <= 0) totalRaw = 16 * 1024 * 1024;
            ByteBuffer raw = ByteBuffer.allocate(totalRaw);
            long ret = Zstd.decompress(raw, ByteBuffer.wrap(info.compressedPayload));
            if (Zstd.isError(ret)) {
                throw new IOException("Zstd decompress error: " + Zstd.getErrorName(ret));
            }
            raw.flip();
            String[] lines = new String[info.lineOffsets.length];
            int[] lengths = BlockCodec.computeLineLengths(info.lineOffsets, raw.remaining());
            for (int i = 0; i < info.lineOffsets.length; i++) {
                raw.position(info.lineOffsets[i]);
                byte[] buf = new byte[lengths[i]];
                raw.get(buf);
                lines[i] = new String(buf, StandardCharsets.UTF_8);
            }
            cachePut(blockId, lines);
            return lines;
        } catch (IOException e) {
            LOG.error("Failed to load block {} from {}", blockId, path, e);
            return null;
        }
    }

    private void cachePut(long blockId, String[] lines) {
        synchronized (cacheLock) {
            blockLineCache.put(blockId, lines);
            cacheLru.addLast(blockId);
            int max = Math.max(1, config.getDecompressCacheSize());
            while (blockLineCache.size() > max) {
                Long victim = cacheLru.pollFirst();
                if (victim == null) break;
                blockLineCache.remove(victim);
            }
        }
    }

    public void invalidateBlock(long blockId) {
        synchronized (cacheLock) {
            blockLineCache.remove(blockId);
            cacheLru.remove(blockId);
        }
    }

    private long elapsedMs(long startNs) {
        return (System.nanoTime() - startNs) / 1_000_000L;
    }
}
