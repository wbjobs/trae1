package com.logservice.storage;

import com.logservice.model.LineMetadata;
import org.roaringbitmap.longlong.Roaring64NavigableMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.regex.Pattern;

public class IndexManager {
    private static final Logger LOG = LoggerFactory.getLogger(IndexManager.class);
    private static final Pattern WORD_PATTERN = Pattern.compile("[a-zA-Z0-9_\\-\\.]+");

    private final ConcurrentHashMap<String, Roaring64NavigableMap> termIndex = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Long, LineMetadata> lineMeta = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Long, String> blockFileMap = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<Long, Long> blockTimestampMap = new ConcurrentHashMap<>();
    private final AtomicLong totalLines = new AtomicLong(0L);

    public void indexBlock(List<LineMetadata> metas, List<String> lines, long blockId, String fileName) {
        if (metas.size() != lines.size()) {
            LOG.warn("Meta/line size mismatch in block {}: {} vs {}", blockId, metas.size(), lines.size());
            return;
        }
        for (int i = 0; i < metas.size(); i++) {
            LineMetadata m = metas.get(i);
            lineMeta.put(m.getLineId(), m);
            String line = lines.get(i);
            indexLine(m.getLineId(), line);
        }
        blockFileMap.put(blockId, fileName);
        if (!metas.isEmpty()) {
            blockTimestampMap.put(blockId, metas.get(0).getTimestamp());
        }
        totalLines.addAndGet(metas.size());
    }

    private void indexLine(long lineId, String line) {
        if (line == null || line.isEmpty()) return;
        var m = WORD_PATTERN.matcher(line);
        while (m.find()) {
            String word = m.group().toLowerCase();
            if (word.isEmpty()) continue;
            termIndex.computeIfAbsent(word, k -> new Roaring64NavigableMap()).addLong(lineId);
        }
    }

    public Roaring64NavigableMap getTermBitmap(String term) {
        if (term == null) return null;
        return termIndex.get(term.toLowerCase());
    }

    public String[] tokenizePhrase(String phrase) {
        if (phrase == null || phrase.isEmpty()) return new String[0];
        return WORD_PATTERN.matcher(phrase).results()
                .map(r -> r.group().toLowerCase())
                .toArray(String[]::new);
    }

    public LineMetadata getLineMeta(long lineId) { return lineMeta.get(lineId); }
    public String getBlockFileName(long blockId) { return blockFileMap.get(blockId); }
    public Long getBlockTimestamp(long blockId) { return blockTimestampMap.get(blockId); }

    public long getTotalLines() { return totalLines.get(); }
    public int getTermCount() { return termIndex.size(); }
    public int getBlockCount() { return blockFileMap.size(); }

    public void dropBlock(long blockId) {
        blockFileMap.remove(blockId);
        blockTimestampMap.remove(blockId);
        List<Long> toRemove = new ArrayList<>();
        for (Map.Entry<Long, LineMetadata> e : lineMeta.entrySet()) {
            if (e.getValue().getBlockId() == blockId) toRemove.add(e.getKey());
        }
        for (Long lid : toRemove) {
            lineMeta.remove(lid);
            totalLines.decrementAndGet();
        }
        for (Map.Entry<String, Roaring64NavigableMap> e : termIndex.entrySet()) {
            Roaring64NavigableMap bm = e.getValue();
            for (Long lid : toRemove) bm.removeLong(lid);
        }
        termIndex.entrySet().removeIf(e -> e.getValue().isEmpty());
    }

    public Iterable<Map.Entry<Long, Long>> blocks() {
        return new ArrayList<>(blockTimestampMap.entrySet());
    }
}
