package com.clickstream.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class DashboardStats {

    private long totalSessions;

    private long totalAnomalies;

    private long blacklistedUsers;

    private long blacklistedIps;

    private double anomalyRate;

    private List<AnomalyTrendPoint> trendPoints;

    private List<String> recentBlacklistReasons;
}
