package com.logservice.storage;

import com.logservice.config.ServiceConfig;
import com.logservice.model.AggregateResult;
import com.logservice.model.AggregateResult.BucketResult;
import com.logservice.model.MinuteBucket;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.ConcurrentNavigableMap;
import java.util.concurrent.ConcurrentSkipListMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

public class AggregationManager {
    private static final Logger LOG = LoggerFactory.getLogger(AggregationManager.class);
    private static final int TOP_WORDS_PER_MINUTE = 200;
    private static final int TOP_WORDS_RESPONSE = 10;
    private static final long ONE_MINUTE_MS = 60_000L;
    private static final long MAX_BUCKETS = 24 * 60;

    private final ServiceConfig config;
    private final Pattern levelPattern;
    private final Pattern modulePattern;
    private final Pattern wordPattern;

    private final ConcurrentSkipListMap<Long, MinuteBucket> completedBuckets = new ConcurrentSkipListMap<>();
    private volatile MinuteBucket currentBucket;
    private final Object rollLock = new Object();

    public AggregationManager(ServiceConfig config) {
        this.config = config;
        this.levelPattern = Pattern.compile(config.getAggregateLevelRegex(), Pattern.CASE_INSENSITIVE);
        this.modulePattern = Pattern.compile(config.getAggregateModuleRegex());
        this.wordPattern = Pattern.compile(config.getAggregateWordRegex());
        long now = System.currentTimeMillis();
        this.currentBucket = new MinuteBucket(alignToMinute(now));
    }

    public void recordLine(String line, long timestampMs) {
        if (line == null || line.isEmpty()) return;
        long minuteKey = alignToMinute(timestampMs);
        MinuteBucket bucket = currentBucket;
        if (bucket.minuteStartMs != minuteKey) {
            synchronized (rollLock) {
                if (currentBucket.minuteStartMs != minuteKey) {
                    rollOverInternal(timestampMs);
                }
                bucket = currentBucket;
            }
        }
        bucket.count++;

        Matcher lm = levelPattern.matcher(line);
        if (lm.find()) bucket.incrementLevel(lm.group(1).toUpperCase());

        Matcher mm = modulePattern.matcher(line);
        if (mm.find()) bucket.incrementModule(mm.group(1).toLowerCase());

        Matcher wm = wordPattern.matcher(line);
        while (wm.find()) {
            String w = wm.group().toLowerCase();
            if (w.length() >= 2) bucket.incrementWord(w);
        }
    }

    public void rollOver() {
        synchronized (rollLock) {
            rollOverInternal(System.currentTimeMillis());
        }
    }

    private void rollOverInternal(long nowMs) {
        MinuteBucket old = currentBucket;
        if (old.count > 0) {
            Map<String, Long> topWords = topNWords(old.wordCounts, TOP_WORDS_PER_MINUTE);
            old.wordCounts.clear();
            old.wordCounts.putAll(topWords);
            completedBuckets.put(old.minuteStartMs, old);
            trimOldBuckets();
        }
        long newMinute = alignToMinute(nowMs);
        currentBucket = new MinuteBucket(newMinute);
    }

    private void trimOldBuckets() {
        long cutoff = System.currentTimeMillis() - config.getAggregationRetentionMs();
        long cutoffMinute = alignToMinute(cutoff);
        NavigableSet<Long> toRemove = completedBuckets.headMap(cutoffMinute).keySet();
        for (Long k : new ArrayList<>(toRemove)) {
            completedBuckets.remove(k);
        }
        if (completedBuckets.size() > MAX_BUCKETS) {
            int excess = completedBuckets.size() - (int) MAX_BUCKETS;
            Iterator<Long> it = completedBuckets.keySet().iterator();
            for (int i = 0; i < excess && it.hasNext(); i++) {
                it.next();
                it.remove();
            }
        }
    }

    public AggregateResult query(long fromMs, long toMs, String bucketSize) {
        long startNs = System.nanoTime();
        long timeoutNs = config.getAggregationTimeoutMs() * 1_000_000L;

        long fromMinute = alignToMinute(fromMs);
        long toMinute = alignToMinute(toMs);

        long bucketMs = parseBucketSize(bucketSize);
        if (bucketMs < ONE_MINUTE_MS) bucketMs = ONE_MINUTE_MS;

        List<MinuteBucket> source = new ArrayList<>();
        synchronized (rollLock) {
            ConcurrentNavigableMap<Long, MinuteBucket> range = completedBuckets.subMap(fromMinute, true, toMinute, true);
            source.addAll(range.values());
            MinuteBucket cur = currentBucket;
            if (cur.minuteStartMs >= fromMinute && cur.minuteStartMs <= toMinute && cur.count > 0) {
                source.add(cur);
            }
        }

        source.sort(Comparator.comparingLong(b -> b.minuteStartMs));

        List<BucketResult> results = new ArrayList<>();
        boolean truncated = false;
        long bucketStart = fromMinute;
        List<MinuteBucket> currentGroup = new ArrayList<>();

        for (MinuteBucket mb : source) {
            if (System.nanoTime() - startNs > timeoutNs) {
                truncated = true;
                break;
            }
            while (mb.minuteStartMs >= bucketStart + bucketMs) {
                if (!currentGroup.isEmpty()) {
                    results.add(mergeGroup(bucketStart, bucketStart + bucketMs, currentGroup));
                    currentGroup.clear();
                }
                bucketStart += bucketMs;
            }
            currentGroup.add(mb);
        }

        if (!currentGroup.isEmpty() && !truncated) {
            results.add(mergeGroup(bucketStart, bucketStart + bucketMs, currentGroup));
        }

        long elapsedMs = (System.nanoTime() - startNs) / 1_000_000L;
        return new AggregateResult(fromMs, toMs, bucketSize, results, elapsedMs, truncated);
    }

    private BucketResult mergeGroup(long startMs, long endMs, List<MinuteBucket> group) {
        long count = 0;
        Map<String, Long> levels = new HashMap<>();
        Map<String, Long> modules = new HashMap<>();
        Map<String, Long> words = new HashMap<>();
        for (MinuteBucket mb : group) {
            count += mb.count;
            for (Map.Entry<String, Long> e : mb.levelCounts.entrySet())
                levels.merge(e.getKey(), e.getValue(), Long::sum);
            for (Map.Entry<String, Long> e : mb.moduleCounts.entrySet())
                modules.merge(e.getKey(), e.getValue(), Long::sum);
            for (Map.Entry<String, Long> e : mb.wordCounts.entrySet())
                words.merge(e.getKey(), e.getValue(), Long::sum);
        }
        Map<String, Double> percentages = new HashMap<>();
        if (count > 0) {
            for (Map.Entry<String, Long> e : levels.entrySet()) {
                percentages.put(e.getKey(), Math.round(e.getValue() * 10000.0 / count) / 100.0);
            }
        }
        List<Map.Entry<String, Long>> topWords = topNWords(words, TOP_WORDS_RESPONSE);
        return new BucketResult(startMs, endMs, count, levels, percentages, modules, topWords);
    }

    private Map<String, Long> topNWords(Map<String, Long> words, int n) {
        return words.entrySet().stream()
                .sorted(Map.Entry.<String, Long>comparingByValue().reversed())
                .limit(n)
                .collect(Collectors.toMap(
                        Map.Entry::getKey,
                        Map.Entry::getValue,
                        (a, b) -> a,
                        LinkedHashMap::new
                ));
    }

    private long parseBucketSize(String s) {
        if (s == null) return ONE_MINUTE_MS;
        s = s.toLowerCase().trim();
        switch (s) {
            case "5m": return 5 * ONE_MINUTE_MS;
            case "1h": case "60m": return 60 * ONE_MINUTE_MS;
            case "1m": default: return ONE_MINUTE_MS;
        }
    }

    private long alignToMinute(long ts) {
        return ts / ONE_MINUTE_MS * ONE_MINUTE_MS;
    }

    public int getCompletedBucketCount() { return completedBuckets.size(); }
    public long getCurrentMinuteCount() { return currentBucket.count; }
    public long getCurrentMinuteStart() { return currentBucket.minuteStartMs; }
}
