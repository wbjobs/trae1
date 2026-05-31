package com.clickstream.model;

import com.fasterxml.jackson.annotation.JsonFormat;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class BlacklistEntry {

    private String userId;

    private String ipAddress;

    @JsonFormat(shape = JsonFormat.Shape.STRING)
    private Instant blacklistedAt;

    private String reason;

    private String sourceSessionId;

    private BlacklistStatus status;

    public enum BlacklistStatus {
        ACTIVE,
        EXPIRED
    }

    public static BlacklistEntry fromAnomaly(AnomalySession anomaly) {
        return BlacklistEntry.builder()
                .userId(anomaly.getUserId())
                .ipAddress(anomaly.getIpAddress())
                .blacklistedAt(Instant.now())
                .reason("Anomaly detected: " + anomaly.getAnomalyType() + " - " + anomaly.getDescription())
                .sourceSessionId(anomaly.getSessionId())
                .status(BlacklistStatus.ACTIVE)
                .build();
    }
}
