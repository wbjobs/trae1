package com.clickstream.controller;

import com.clickstream.model.*;
import com.clickstream.service.BlacklistProducerService;
import com.clickstream.service.DashboardService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/dashboard")
public class DashboardController {

    private final DashboardService dashboardService;
    private final BlacklistProducerService blacklistProducerService;

    @Autowired
    public DashboardController(DashboardService dashboardService, 
                              BlacklistProducerService blacklistProducerService) {
        this.dashboardService = dashboardService;
        this.blacklistProducerService = blacklistProducerService;
    }

    @GetMapping("/stats")
    public ResponseEntity<DashboardStats> getDashboardStats(
            @RequestParam(defaultValue = "24") int trendHours) {
        DashboardStats stats = dashboardService.getDashboardStats(trendHours);
        return ResponseEntity.ok(stats);
    }

    @GetMapping("/anomalies/recent")
    public ResponseEntity<List<AnomalySession>> getRecentAnomalies(
            @RequestParam(defaultValue = "50") int limit) {
        List<AnomalySession> anomalies = dashboardService.getRecentAnomalies(limit);
        return ResponseEntity.ok(anomalies);
    }

    @GetMapping("/blacklist/recent")
    public ResponseEntity<List<BlacklistEntry>> getRecentBlacklistEntries(
            @RequestParam(defaultValue = "20") int limit) {
        List<BlacklistEntry> entries = dashboardService.getRecentBlacklistEntries(limit);
        return ResponseEntity.ok(entries);
    }

    @GetMapping("/trend")
    public ResponseEntity<List<AnomalyTrendPoint>> getAnomalyTrend(
            @RequestParam(defaultValue = "24") int hours) {
        DashboardStats stats = dashboardService.getDashboardStats(hours);
        return ResponseEntity.ok(stats.getTrendPoints());
    }

    @PostMapping("/blacklist/test")
    public ResponseEntity<Map<String, Object>> testBlacklistPublish(
            @RequestBody Map<String, String> request) {
        String userId = request.getOrDefault("userId", "test-user-001");
        String ipAddress = request.getOrDefault("ipAddress", "192.168.1.100");
        String reason = request.getOrDefault("reason", "Manual test blacklist entry");

        BlacklistEntry entry = BlacklistEntry.builder()
                .userId(userId)
                .ipAddress(ipAddress)
                .reason(reason)
                .sourceSessionId("manual-test")
                .build();

        blacklistProducerService.publishBlacklistEntry(entry);

        Map<String, Object> response = new HashMap<>();
        response.put("status", "success");
        response.put("message", "Blacklist entry published");
        response.put("entry", entry);

        return ResponseEntity.ok(response);
    }

    @GetMapping("/summary")
    public ResponseEntity<Map<String, Object>> getSummary() {
        DashboardStats stats = dashboardService.getDashboardStats(24);
        
        Map<String, Object> summary = new HashMap<>();
        summary.put("totalSessions", stats.getTotalSessions());
        summary.put("totalAnomalies", stats.getTotalAnomalies());
        summary.put("blacklistedUsers", stats.getBlacklistedUsers());
        summary.put("blacklistedIps", stats.getBlacklistedIps());
        summary.put("anomalyRate", String.format("%.2f%%", stats.getAnomalyRate()));
        
        return ResponseEntity.ok(summary);
    }
}
