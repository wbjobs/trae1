package com.logservice.model;

import java.util.List;
import java.util.Map;

public class AggregateResult {
    private final long fromMs;
    private final long toMs;
    private final String bucketSize;
    private final List<BucketResult> buckets;
    private final long elapsedMs;
    private final boolean truncated;

    public AggregateResult(long fromMs, long toMs, String bucketSize, List<BucketResult> buckets,
                            long elapsedMs, boolean truncated) {
        this.fromMs = fromMs;
        this.toMs = toMs;
        this.bucketSize = bucketSize;
        this.buckets = buckets;
        this.elapsedMs = elapsedMs;
        this.truncated = truncated;
    }

    public long getFromMs() { return fromMs; }
    public long getToMs() { return toMs; }
    public String getBucketSize() { return bucketSize; }
    public List<BucketResult> getBuckets() { return buckets; }
    public long getElapsedMs() { return elapsedMs; }
    public boolean isTruncated() { return truncated; }

    public static class BucketResult {
        private final long startMs;
        private final long endMs;
        private final long count;
        private final Map<String, Long> levels;
        private final Map<String, Double> levelPercentages;
        private final Map<String, Long> modules;
        private final List<Map.Entry<String, Long>> topWords;

        public BucketResult(long startMs, long endMs, long count,
                            Map<String, Long> levels, Map<String, Double> levelPercentages,
                            Map<String, Long> modules, List<Map.Entry<String, Long>> topWords) {
            this.startMs = startMs;
            this.endMs = endMs;
            this.count = count;
            this.levels = levels;
            this.levelPercentages = levelPercentages;
            this.modules = modules;
            this.topWords = topWords;
        }

        public long getStartMs() { return startMs; }
        public long getEndMs() { return endMs; }
        public long getCount() { return count; }
        public Map<String, Long> getLevels() { return levels; }
        public Map<String, Double> getLevelPercentages() { return levelPercentages; }
        public Map<String, Long> getModules() { return modules; }
        public List<Map.Entry<String, Long>> getTopWords() { return topWords; }
    }
}
