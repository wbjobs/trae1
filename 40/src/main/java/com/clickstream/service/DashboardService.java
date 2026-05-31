package com.clickstream.service;

import com.clickstream.model.*;
import com.clickstream.store.AnomalyStore;
import com.clickstream.store.BlacklistStore;
import com.clickstream.store.SessionStore;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.util.List;
import java.util.stream.Collectors;

@Service
public class DashboardService {

    private final SessionStore sessionStore;
    private final AnomalyStore anomalyStore;
    private final BlacklistStore blacklistStore;

    @Autowired
    public DashboardService(SessionStore sessionStore, 
                           AnomalyStore anomalyStore,
                           BlacklistStore blacklistStore) {
        this.sessionStore = sessionStore;
        this.anomalyStore = anomalyStore;
        this.blacklistStore = blacklistStore;
    }

    public DashboardStats getDashboardStats(int trendHours) {
        long totalSessions = sessionStore.getSessionCount();
        long totalAnomalies = anomalyStore.getTotalAnomalyCount();
        long blacklistedUsers = blacklistStore.getBlacklistedUserCount();
        long blacklistedIps = blacklistStore.getBlacklistedIpCount();
        double anomalyRate = totalSessions > 0 ? (double) totalAnomalies / totalSessions * 100 : 0.0;

        List<AnomalyTrendPoint> trendPoints = anomalyStore.getTrendData(trendHours);
        
        List<String> recentReasons = blacklistStore.getRecentBlacklistEntries(10).stream()
                .map(BlacklistEntry::getReason)
                .collect(Collectors.toList());

        return DashboardStats.builder()
                .totalSessions(totalSessions)
                .totalAnomalies(totalAnomalies)
                .blacklistedUsers(blacklistedUsers)
                .blacklistedIps(blacklistedIps)
                .anomalyRate(anomalyRate)
                .trendPoints(trendPoints)
                .recentBlacklistReasons(recentReasons)
                .build();
    }

    public List<AnomalySession> getRecentAnomalies(int limit) {
        return anomalyStore.getRecentAnomalies(limit);
    }

    public List<BlacklistEntry> getRecentBlacklistEntries(int limit) {
        return blacklistStore.getRecentBlacklistEntries(limit);
    }
}
