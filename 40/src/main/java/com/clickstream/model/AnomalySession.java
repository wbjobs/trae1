package com.clickstream.model;

import com.fasterxml.jackson.annotation.JsonFormat;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;
import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class AnomalySession {

    private String sessionId;

    private String userId;

    private String ipAddress;

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant detectionTime;

    private AnomalyType anomalyType;

    private String description;

    private double avgPageDurationSeconds;

    private int concurrentSessionCount;

    private double pathRepeatabilityScore;

    private List<String> detectedSignals;

    public enum AnomalyType {
        HIGH_CONCURRENCY,
        LOW_PAGE_DURATION,
        REPETITIVE_PATH,
        CRAWLER
    }
}
