package com.logservice.model;

import java.util.HashMap;
import java.util.Map;

public class MinuteBucket {
    public final long minuteStartMs;
    public long count;
    public final Map<String, Long> levelCounts;
    public final Map<String, Long> moduleCounts;
    public final Map<String, Long> wordCounts;

    public MinuteBucket(long minuteStartMs) {
        this.minuteStartMs = minuteStartMs;
        this.count = 0L;
        this.levelCounts = new HashMap<>();
        this.moduleCounts = new HashMap<>();
        this.wordCounts = new HashMap<>();
    }

    public void incrementLevel(String level) {
        if (level == null) return;
        levelCounts.merge(level, 1L, Long::sum);
    }

    public void incrementModule(String module) {
        if (module == null) return;
        moduleCounts.merge(module, 1L, Long::sum);
    }

    public void incrementWord(String word) {
        if (word == null || word.isEmpty()) return;
        wordCounts.merge(word, 1L, Long::sum);
    }
}
