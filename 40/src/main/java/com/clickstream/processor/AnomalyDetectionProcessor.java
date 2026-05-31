package com.clickstream.processor;

import com.clickstream.config.KafkaStreamsConfig;
import com.clickstream.model.*;
import com.clickstream.store.AnomalyStore;
import com.clickstream.store.BlacklistStore;
import org.apache.kafka.common.serialization.Serdes;
import org.apache.kafka.streams.StreamsBuilder;
import org.apache.kafka.streams.kstream.*;
import org.apache.kafka.streams.kstream.internals.TimeWindow;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import java.time.Duration;
import java.time.Instant;
import java.util.*;

@Component
public class AnomalyDetectionProcessor {

    private static final Logger logger = LoggerFactory.getLogger(AnomalyDetectionProcessor.class);

    @Value("${kafka.output.topic:sessions}")
    private String sessionsTopic;

    @Value("${kafka.output.detail.topic:session-details}")
    private String sessionDetailsTopic;

    @Value("${kafka.anomaly.topic:anomaly-sessions}")
    private String anomalyTopic;

    @Value("${kafka.blacklist.topic:blacklist-entries}")
    private String blacklistTopic;

    @Value("${anomaly.concurrent.threshold:5}")
    private int concurrentThreshold;

    @Value("${anomaly.page.duration.threshold.seconds:1}")
    private double pageDurationThreshold;

    @Value("${anomaly.path.repeat.threshold:0.8}")
    private double pathRepeatThreshold;

    @Value("${anomaly.concurrent.window.minutes:5}")
    private long concurrentWindowMinutes;

    private final KafkaStreamsConfig.JsonSerde<SessionDetail> sessionDetailSerde;
    private final KafkaStreamsConfig.JsonSerde<AnomalySession> anomalySerde;
    private final KafkaStreamsConfig.JsonSerde<BlacklistEntry> blacklistEntrySerde;
    private final KafkaStreamsConfig.JsonSerde<IpSessionCount> ipSessionCountSerde;
    private final AnomalyStore anomalyStore;
    private final BlacklistStore blacklistStore;

    @Autowired
    public AnomalyDetectionProcessor(
            KafkaStreamsConfig.JsonSerde<SessionDetail> sessionDetailSerde,
            KafkaStreamsConfig.JsonSerde<AnomalySession> anomalySerde,
            KafkaStreamsConfig.JsonSerde<BlacklistEntry> blacklistEntrySerde,
            KafkaStreamsConfig.JsonSerde<IpSessionCount> ipSessionCountSerde,
            AnomalyStore anomalyStore,
            BlacklistStore blacklistStore) {
        this.sessionDetailSerde = sessionDetailSerde;
        this.anomalySerde = anomalySerde;
        this.blacklistEntrySerde = blacklistEntrySerde;
        this.ipSessionCountSerde = ipSessionCountSerde;
        this.anomalyStore = anomalyStore;
        this.blacklistStore = blacklistStore;
    }

    public void buildAnomalyDetectionTopology(StreamsBuilder streamsBuilder) {
        Duration concurrentWindow = Duration.ofMinutes(concurrentWindowMinutes);

        KStream<String, SessionDetail> sessionDetails = streamsBuilder
                .stream(sessionDetailsTopic, Consumed.with(Serdes.String(), sessionDetailSerde))
                .filter((key, detail) -> detail != null && detail.getUserId() != null)
                .peek((key, detail) -> logger.debug("Analyzing session for anomalies: sessionId={}", detail.getSessionId()));

        detectLowPageDuration(sessionDetails);
        detectRepetitivePath(sessionDetails);
        detectHighConcurrency(sessionDetails, concurrentWindow);
    }

    private void detectLowPageDuration(KStream<String, SessionDetail> sessionDetails) {
        sessionDetails
                .filter((key, detail) -> {
                    double avgDuration = calculateAveragePageDuration(detail);
                    return avgDuration > 0 && avgDuration < pageDurationThreshold && detail.getPageCount() >= 3;
                })
                .mapValues(this::createLowDurationAnomaly)
                .filter((key, anomaly) -> anomaly != null)
                .peek((key, anomaly) -> {
                    logger.warn("Anomaly detected (LOW_PAGE_DURATION): sessionId={}, userId={}, avgDuration={}s",
                            anomaly.getSessionId(), anomaly.getUserId(), anomaly.getAvgPageDurationSeconds());
                    anomalyStore.recordAnomaly(anomaly);
                })
                .to(anomalyTopic, Produced.with(Serdes.String(), anomalySerde));
    }

    private void detectRepetitivePath(KStream<String, SessionDetail> sessionDetails) {
        sessionDetails
                .filter((key, detail) -> {
                    double repeatability = calculatePathRepeatability(detail);
                    return repeatability >= pathRepeatThreshold && detail.getPageCount() >= 5;
                })
                .mapValues(this::createRepetitivePathAnomaly)
                .filter((key, anomaly) -> anomaly != null)
                .peek((key, anomaly) -> {
                    logger.warn("Anomaly detected (REPETITIVE_PATH): sessionId={}, userId={}, repeatability={}",
                            anomaly.getSessionId(), anomaly.getUserId(), anomaly.getPathRepeatabilityScore());
                    anomalyStore.recordAnomaly(anomaly);
                })
                .to(anomalyTopic, Produced.with(Serdes.String(), anomalySerde));
    }

    private void detectHighConcurrency(KStream<String, SessionDetail> sessionDetails, Duration window) {
        sessionDetails
                .filter((key, detail) -> detail.getUserId() != null)
                .selectKey((key, detail) -> extractIpFromUserAgent(detail.getUserAgent()))
                .groupByKey(Grouped.with(Serdes.String(), sessionDetailSerde))
                .windowedBy(TimeWindows.ofSizeWithNoGrace(window))
                .count()
                .toStream()
                .filter((windowedKey, count) -> count != null && count > concurrentThreshold)
                .mapValues((windowedKey, count) -> {
                    String ip = windowedKey.key();
                    return IpSessionCount.builder()
                            .ipAddress(ip)
                            .sessionCount(count.intValue())
                            .windowStart(windowedKey.window().start())
                            .windowEnd(windowedKey.window().end())
                            .build();
                })
                .map((windowedKey, ipCount) -> {
                    AnomalySession anomaly = AnomalySession.builder()
                            .sessionId("CONCURRENT-" + windowedKey.key() + "-" + windowedKey.window().start())
                            .userId("multiple-users")
                            .ipAddress(windowedKey.key())
                            .detectionTime(Instant.now())
                            .anomalyType(AnomalySession.AnomalyType.HIGH_CONCURRENCY)
                            .description("High concurrency from IP: " + ipCount.getSessionCount() + " sessions in " + concurrentWindowMinutes + "min window")
                            .concurrentSessionCount(ipCount.getSessionCount())
                            .detectedSignals(Arrays.asList("CONCURRENT_SESSIONS_EXCEEDED"))
                            .build();
                    return new KeyValue<>(windowedKey.key(), anomaly);
                })
                .peek((key, anomaly) -> {
                    logger.warn("Anomaly detected (HIGH_CONCURRENCY): ip={}, sessions={}", 
                            anomaly.getIpAddress(), anomaly.getConcurrentSessionCount());
                    anomalyStore.recordAnomaly(anomaly);
                })
                .to(anomalyTopic, Produced.with(Serdes.String(), anomalySerde));
    }

    private double calculateAveragePageDuration(SessionDetail detail) {
        if (detail.getPageSequence() == null || detail.getPageSequence().size() < 2) {
            return Double.MAX_VALUE;
        }

        List<SessionDetail.PageView> pageViews = detail.getPageSequence();
        long totalDuration = 0;
        int intervals = 0;

        for (int i = 1; i < pageViews.size(); i++) {
            Instant prev = pageViews.get(i - 1).getTimestamp();
            Instant curr = pageViews.get(i).getTimestamp();
            if (prev != null && curr != null) {
                totalDuration += Duration.between(prev, curr).toMillis();
                intervals++;
            }
        }

        return intervals > 0 ? (totalDuration / (double) intervals) / 1000.0 : Double.MAX_VALUE;
    }

    private double calculatePathRepeatability(SessionDetail detail) {
        if (detail.getPageSequence() == null || detail.getPageSequence().isEmpty()) {
            return 0.0;
        }

        Map<String, Integer> pageCounts = new HashMap<>();
        int totalPages = detail.getPageSequence().size();

        for (SessionDetail.PageView pv : detail.getPageSequence()) {
            pageCounts.merge(pv.getPageUrl(), 1, Integer::sum);
        }

        Optional<Map.Entry<String, Integer>> maxEntry = pageCounts.entrySet().stream()
                .max(Map.Entry.comparingByValue());

        return maxEntry.map(entry -> (double) entry.getValue() / totalPages).orElse(0.0);
    }

    private AnomalySession createLowDurationAnomaly(SessionDetail detail) {
        double avgDuration = calculateAveragePageDuration(detail);
        
        List<String> signals = new ArrayList<>();
        signals.add("LOW_AVERAGE_PAGE_DURATION");
        if (detail.getPageCount() > 10) {
            signals.add("HIGH_PAGE_COUNT");
        }

        return AnomalySession.builder()
                .sessionId(detail.getSessionId())
                .userId(detail.getUserId())
                .ipAddress(extractIpFromUserAgent(detail.getUserAgent()))
                .detectionTime(Instant.now())
                .anomalyType(AnomalySession.AnomalyType.LOW_PAGE_DURATION)
                .description(String.format("Average page duration %.2fs is below threshold %.2fs", 
                        avgDuration, pageDurationThreshold))
                .avgPageDurationSeconds(avgDuration)
                .detectedSignals(signals)
                .build();
    }

    private AnomalySession createRepetitivePathAnomaly(SessionDetail detail) {
        double repeatability = calculatePathRepeatability(detail);

        return AnomalySession.builder()
                .sessionId(detail.getSessionId())
                .userId(detail.getUserId())
                .ipAddress(extractIpFromUserAgent(detail.getUserAgent()))
                .detectionTime(Instant.now())
                .anomalyType(AnomalySession.AnomalyType.REPETITIVE_PATH)
                .description(String.format("Path repeatability %.2f exceeds threshold %.2f", 
                        repeatability, pathRepeatThreshold))
                .pathRepeatabilityScore(repeatability)
                .detectedSignals(Arrays.asList("HIGH_PATH_REPEATABILITY"))
                .build();
    }

    private String extractIpFromUserAgent(String userAgent) {
        if (userAgent == null || userAgent.isEmpty()) {
            return "unknown";
        }
        return userAgent.hashCode() + "-" + userAgent.substring(0, Math.min(8, userAgent.length()));
    }

    @lombok.Data
    @lombok.Builder
    @lombok.NoArgsConstructor
    @lombok.AllArgsConstructor
    public static class IpSessionCount {
        private String ipAddress;
        private int sessionCount;
        private long windowStart;
        private long windowEnd;
    }
}
