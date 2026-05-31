package com.dbagent.indexadvisor;

import java.sql.Connection;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

public class SlowSqlDetector {

    private static volatile SlowSqlDetector instance;
    private static final Object LOCK = new Object();

    private final IndexAdvisor indexAdvisor;
    private final IndexExecutionManager executionManager;
    private final ConcurrentHashMap<String, Long> recentSuggestions = new ConcurrentHashMap<>();
    private final AtomicLong totalSlowSqlDetected = new AtomicLong(0);
    private final AtomicLong totalIndexSuggestionsGenerated = new AtomicLong(0);

    private long slowSqlThresholdMs = 200;
    private long suggestionCooldownMs = 300000;
    private boolean enabled = false;

    private SlowSqlDetector() {
        this.indexAdvisor = new IndexAdvisor();
        this.executionManager = IndexExecutionManager.getInstance();
    }

    public static SlowSqlDetector getInstance() {
        if (instance == null) {
            synchronized (LOCK) {
                if (instance == null) {
                    instance = new SlowSqlDetector();
                }
            }
        }
        return instance;
    }

    public void initialize(long slowSqlThresholdMs, long suggestionCooldownMs,
                           WebhookNotifier webhookNotifier, boolean autoExecute) {
        this.slowSqlThresholdMs = slowSqlThresholdMs > 0 ? slowSqlThresholdMs : 200;
        this.suggestionCooldownMs = suggestionCooldownMs > 0 ? suggestionCooldownMs : 300000;
        this.enabled = true;

        executionManager.initialize(webhookNotifier, autoExecute, 86400000);

        System.out.println("[DB-Tracing-Agent] SlowSqlDetector initialized, threshold: "
                + this.slowSqlThresholdMs + "ms, cooldown: " + this.suggestionCooldownMs + "ms");
    }

    public void detectAndAnalyze(String sql, String databaseType,
                                 Connection connection, long executionTimeMs) {
        if (!enabled || sql == null || executionTimeMs < slowSqlThresholdMs) {
            return;
        }

        totalSlowSqlDetected.incrementAndGet();

        String sqlKey = generateSqlKey(sql);
        long now = System.currentTimeMillis();
        Long lastSuggestionTime = recentSuggestions.get(sqlKey);

        if (lastSuggestionTime != null && (now - lastSuggestionTime) < suggestionCooldownMs) {
            return;
        }

        try {
            List<IndexSuggestion> suggestions = indexAdvisor.analyze(
                    sql, databaseType, connection, executionTimeMs);

            if (suggestions != null && !suggestions.isEmpty()) {
                recentSuggestions.put(sqlKey, now);
                totalIndexSuggestionsGenerated.addAndGet(suggestions.size());

                for (IndexSuggestion suggestion : suggestions) {
                    System.out.println("[DB-Tracing-Agent] Slow SQL detected ("
                            + executionTimeMs + "ms): "
                            + (sql.length() > 100 ? sql.substring(0, 100) + "..." : sql));
                    System.out.println("[DB-Tracing-Agent] Index suggestion: "
                            + suggestion.getCreateIndexSql());

                    executionManager.submitSuggestion(suggestion, connection);
                }
            }
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Error analyzing slow SQL: "
                    + e.getMessage());
        }
    }

    private String generateSqlKey(String sql) {
        String normalized = sql.replaceAll("\\s+", " ")
                .replaceAll("'[^']*'", "?")
                .replaceAll("\\d+", "?")
                .trim()
                .toLowerCase();

        if (normalized.length() > 200) {
            normalized = normalized.substring(0, 200);
        }
        return normalized;
    }

    public long getSlowSqlThresholdMs() {
        return slowSqlThresholdMs;
    }

    public void setSlowSqlThresholdMs(long slowSqlThresholdMs) {
        this.slowSqlThresholdMs = slowSqlThresholdMs;
    }

    public long getSuggestionCooldownMs() {
        return suggestionCooldownMs;
    }

    public void setSuggestionCooldownMs(long suggestionCooldownMs) {
        this.suggestionCooldownMs = suggestionCooldownMs;
    }

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public long getTotalSlowSqlDetected() {
        return totalSlowSqlDetected.get();
    }

    public long getTotalIndexSuggestionsGenerated() {
        return totalIndexSuggestionsGenerated.get();
    }

    public void shutdown() {
        executionManager.shutdown();
        enabled = false;
    }
}
