package com.configcenter.server.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class GrayscaleBatchEntity {
    private String batchId;
    private ConfigKeyEntity key;
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

    public static String statusActive() { return "ACTIVE"; }
    public static String statusPromoted() { return "PROMOTED"; }
    public static String statusCancelled() { return "CANCELLED"; }
    public static String statusAutoPromoted() { return "AUTO_PROMOTED"; }
    public static String statusAutoCancelled() { return "AUTO_CANCELLED"; }

    public boolean isActive() {
        return statusActive().equals(status);
    }
}
