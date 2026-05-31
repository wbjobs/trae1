package com.clickstream.store;

import com.clickstream.model.AnomalySession;
import com.clickstream.model.AnomalyTrendPoint;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;

@Component
public class AnomalyStore {

    private static final Logger logger = LoggerFactory.getLogger(AnomalyStore.class);

    private final List<AnomalySession> recentAnomalies = new CopyOnWriteArrayList<>();
    private final Map<Instant, Map<AnomalySession.AnomalyType, Long>> trendData = new LinkedHashMap<>();
    
    private static final int DATA_RETENTION_HOURS = 24;
    private static final int MAX_RECENT_ANOMALIES = 1000;

    public void recordAnomaly(AnomalySession anomaly) {
        recentAnomalies.add(anomaly);

        Instant windowStart = anomaly.getDetectionTime().truncatedTo(ChronoUnit.MINUTES);
        trendData.computeIfAbsent(windowStart, k -> new ConcurrentHashMap<>())
                .merge(anomaly.getAnomalyType(), 1L, Long::sum);

        trimRecentAnomalies();
    }

    public List<AnomalySession> getRecentAnomalies(int limit) {
        int size = recentAnomalies.size();
        int start = Math.max(0, size - limit);
        List<AnomalySession> result = new ArrayList<>();
        for (int i = size - 1; i >= start; i--) {
            result.add(recentAnomalies.get(i));
        }
        return result;
    }

    public List<AnomalyTrendPoint> getTrendData(int lastHours) {
        Instant threshold = Instant.now().minus(lastHours, ChronoUnit.HOURS);
        List<AnomalyTrendPoint> points = new ArrayList<>();

        for (Map.Entry<Instant, Map<AnomalySession.AnomalyType, Long>> entry : trendData.entrySet()) {
            if (entry.getKey().isAfter(threshold)) {
                Map<AnomalySession.AnomalyType, Long> counts = entry.getValue();
                points.add(AnomalyTrendPoint.builder()
                        .timestamp(entry.getKey())
                        .anomalyCount(counts.values().stream().mapToLong(Long::longValue).sum())
                        .highConcurrencyCount(counts.getOrDefault(AnomalySession.AnomalyType.HIGH_CONCURRENCY, 0L))
                        .lowDurationCount(counts.getOrDefault(AnomalySession.AnomalyType.LOW_PAGE_DURATION, 0L))
                        .repetitivePathCount(counts.getOrDefault(AnomalySession.AnomalyType.REPETITIVE_PATH, 0L))
                        .crawlerCount(counts.getOrDefault(AnomalySession.AnomalyType.CRAWLER, 0L))
                        .build());
            }
        }

        return points;
    }

    public long getTotalAnomalyCount() {
        return recentAnomalies.size();
    }

    public Map<AnomalySession.AnomalyType, Long> getAnomalyTypeCounts() {
        Map<AnomalySession.AnomalyType, Long> counts = new EnumMap<>(AnomalySession.AnomalyType.class);
        for (AnomalySession anomaly : recentAnomalies) {
            counts.merge(anomaly.getAnomalyType(), 1L, Long::sum);
        }
        return counts;
    }

    private void trimRecentAnomalies() {
        while (recentAnomalies.size() > MAX_RECENT_ANOMALIES) {
            recentAnomalies.remove(0);
        }
    }

    @Scheduled(fixedRate = 3600000)
    public void cleanExpiredTrendData() {
        Instant threshold = Instant.now().minus(DATA_RETENTION_HOURS, ChronoUnit.HOURS);
        trendData.entrySet().removeIf(entry -> entry.getKey().isBefore(threshold));
        logger.info("Cleaned expired trend data");
    }
}
