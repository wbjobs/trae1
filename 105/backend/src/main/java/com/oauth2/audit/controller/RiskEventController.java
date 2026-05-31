package com.oauth2.audit.controller;

import com.oauth2.audit.entity.RiskEvent;
import com.oauth2.audit.service.AnomalyDetectionService;
import com.oauth2.audit.service.ModelTrainingScheduler;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/risk-events")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class RiskEventController {
    private final AnomalyDetectionService anomalyDetectionService;
    private final ModelTrainingScheduler modelTrainingScheduler;

    @GetMapping
    public ResponseEntity<List<RiskEvent>> getRecentRiskEvents() {
        return ResponseEntity.ok(anomalyDetectionService.getRecentRiskEvents());
    }

    @GetMapping("/pending")
    public ResponseEntity<List<RiskEvent>> getPendingRiskEvents() {
        return ResponseEntity.ok(anomalyDetectionService.getPendingRiskEvents());
    }

    @GetMapping("/user/{userId}")
    public ResponseEntity<List<RiskEvent>> getUserRiskEvents(@PathVariable Long userId) {
        return ResponseEntity.ok(anomalyDetectionService.getUserRiskEvents(userId));
    }

    @GetMapping("/level/{level}")
    public ResponseEntity<List<RiskEvent>> getRiskEventsByLevel(@PathVariable String level) {
        RiskEvent.RiskLevel riskLevel = RiskEvent.RiskLevel.valueOf(level.toUpperCase());
        return ResponseEntity.ok(anomalyDetectionService.getRiskEventsByLevel(riskLevel));
    }

    @GetMapping("/statistics")
    public ResponseEntity<Map<String, Object>> getRiskStatistics() {
        return ResponseEntity.ok(anomalyDetectionService.getRiskStatistics());
    }

    @PostMapping("/{eventId}/confirm-self")
    public ResponseEntity<Void> confirmAsSelf(@PathVariable Long eventId) {
        anomalyDetectionService.confirmAsSelf(eventId);
        return ResponseEntity.ok().build();
    }

    @PostMapping("/{eventId}/revoke")
    public ResponseEntity<Void> revokeAndConfirm(@PathVariable Long eventId) {
        anomalyDetectionService.revokeAndConfirm(eventId);
        return ResponseEntity.ok().build();
    }

    @PostMapping("/{eventId}/dismiss")
    public ResponseEntity<Void> dismissEvent(@PathVariable Long eventId) {
        anomalyDetectionService.dismissEvent(eventId);
        return ResponseEntity.ok().build();
    }

    @GetMapping("/model/status")
    public ResponseEntity<Map<String, Object>> getModelStatus() {
        return ResponseEntity.ok(Map.of(
            "ready", modelTrainingScheduler.isModelReady(),
            "lastTrainingTime", modelTrainingScheduler.getLastTrainingTime()
        ));
    }

    @PostMapping("/model/train")
    public ResponseEntity<Void> triggerModelTraining() {
        modelTrainingScheduler.triggerManualRetraining();
        return ResponseEntity.ok().build();
    }
}
