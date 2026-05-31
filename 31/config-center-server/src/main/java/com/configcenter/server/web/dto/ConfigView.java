package com.configcenter.server.web.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ConfigView {
    private String application;
    private String profile;
    private String key;
    private String content;
    private long version;
    private String updatedBy;
    private long updatedAt;

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class HistoryItem {
        private long version;
        private String content;
        private String operator;
        private long changedAt;
        private String changeType;
        private String diff;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class KeySummary {
        private String application;
        private String profile;
        private String key;
        private long version;
        private String updatedBy;
        private long updatedAt;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class PublishRequest {
        private String application;
        private String profile;
        private String key;
        private String content;
        private String operator;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class RollbackRequest {
        private String application;
        private String profile;
        private String key;
        private long targetVersion;
        private String operator;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrayscaleStartRequest {
        private String application;
        private String profile;
        private String key;
        private String content;
        private Integer percent;
        private Long observeWindowSec;
        private String operator;
        private Integer errorThresholdPct;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrayscaleBatchView {
        private String batchId;
        private String application;
        private String profile;
        private String key;
        private String content;
        private long targetVersion;
        private int percent;
        private long observeWindowSec;
        private String operator;
        private long createdAt;
        private String status;
        private int errorThresholdPct;
        private long resolvedAt;
        private String resolution;
        private String diff;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class GrayscaleStatusView {
        private GrayscaleBatchView batch;
        private int totalClients;
        private int grayClients;
        private int healthyClients;
        private int errorClients;
        private int upgradedClients;
        private double errorRatePct;
        private long remainingSec;
    }

    @Data
    @Builder
    @NoArgsConstructor
    @AllArgsConstructor
    public static class ClientView {
        private String instanceId;
        private String clientIp;
        private String application;
        private String profile;
        private long version;
        private boolean healthy;
        private String errorMessage;
        private long lastSeenAt;
        private boolean grayscale;
        private String batchId;
    }
}
