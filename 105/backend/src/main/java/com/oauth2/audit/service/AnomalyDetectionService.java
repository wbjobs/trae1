package com.oauth2.audit.service;

import com.oauth2.audit.config.MLConfig;
import com.oauth2.audit.entity.*;
import com.oauth2.audit.repository.AuthorizationFeatureRepository;
import com.oauth2.audit.repository.RiskEventRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.time.LocalTime;
import java.util.*;

@Service
@RequiredArgsConstructor
@Slf4j
public class AnomalyDetectionService {
    private final IsolationForestModelService modelService;
    private final FeatureExtractionService featureExtractionService;
    private final RiskEventRepository riskEventRepository;
    private final AuthorizationFeatureRepository featureRepository;
    private final NotificationService notificationService;
    private final MLConfig mlConfig;

    @Transactional
    public Optional<RiskEvent> detectAnomaly(UserAuthorization authorization) {
        if (!mlConfig.getAnomalyDetection().isEnabled()) {
            return Optional.empty();
        }

        if (!modelService.isModelReady()) {
            log.warn("ML model not ready, skipping anomaly detection");
            return Optional.empty();
        }

        AuthorizationFeature feature = featureExtractionService.extractFeature(authorization);

        double[] featureVector = featureExtractionService.buildFeatureVector(authorization, feature);
        double anomalyScore = modelService.predictAnomalyScore(featureVector);

        List<RiskEvent.RiskType> riskTypes = identifyRiskTypes(authorization, feature, anomalyScore);

        if (anomalyScore < mlConfig.getAnomalyDetection().getThreshold() && riskTypes.isEmpty()) {
            return Optional.empty();
        }

        RiskEvent riskEvent = new RiskEvent();
        riskEvent.setUser(authorization.getUser());
        riskEvent.setAuthorization(authorization);
        riskEvent.setAnomalyScore(anomalyScore);
        riskEvent.setRiskLevel(calculateRiskLevel(anomalyScore, riskTypes));
        riskEvent.setRiskType(determinePrimaryRiskType(riskTypes));
        riskEvent.setRiskReason(buildRiskReason(authorization, feature, riskTypes, anomalyScore));
        riskEvent.setFeatureVector(feature.getFeatureVector());
        riskEvent.setDetectedAt(LocalDateTime.now());
        riskEvent.setStatus(RiskEvent.EventStatus.PENDING);
        riskEvent.setNotifyEmail(authorization.getUser().getEmail());

        riskEvent = riskEventRepository.save(riskEvent);

        if (riskEvent.getRiskLevel().getSeverity() >= RiskEvent.RiskLevel.HIGH.getSeverity()) {
            notificationService.sendAnomalyAlert(riskEvent);
            riskEvent.setNotificationSent(true);
            riskEvent.setNotificationSentAt(LocalDateTime.now());
            riskEventRepository.save(riskEvent);
        }

        log.info("Risk event detected for user {}: score={}, level={}, types={}",
                authorization.getUser().getId(), anomalyScore, riskEvent.getRiskLevel(), riskTypes);

        return Optional.of(riskEvent);
    }

    private List<RiskEvent.RiskType> identifyRiskTypes(UserAuthorization authorization,
                                                       AuthorizationFeature feature,
                                                       double anomalyScore) {
        List<RiskEvent.RiskType> riskTypes = new ArrayList<>();

        LocalTime authTime = authorization.getAuthorizedAt().toLocalTime();
        if (authTime.isAfter(LocalTime.of(23, 0)) || authTime.isBefore(LocalTime.of(5, 0))) {
            riskTypes.add(RiskEvent.RiskType.UNUSUAL_TIME);
        }

        if (feature.getIsNewApplication() != null && feature.getIsNewApplication()) {
            riskTypes.add(RiskEvent.RiskType.NEW_APPLICATION);
        }

        if (feature.getIsNewDevice() != null && feature.getIsNewDevice()) {
            riskTypes.add(RiskEvent.RiskType.UNUSUAL_LOCATION);
        }

        if (authorization.getDuration() != null &&
            authorization.getDuration() == UserAuthorization.AuthorizationDuration.PERMANENT) {
            riskTypes.add(RiskEvent.RiskType.UNUSUAL_DURATION);
        }

        if (authorization.getScopes() != null &&
            authorization.getScopes().stream().anyMatch(s ->
                s.contains("admin") || s.contains("finance") || s.contains("payment"))) {
            riskTypes.add(RiskEvent.RiskType.HIGH_RISK_SCOPE);
        }

        if (feature.getAuthorizationCountLast7Days() != null &&
            feature.getAuthorizationCountLast7Days() > 10) {
            riskTypes.add(RiskEvent.RiskType.RAPID_SUCCESSION);
        }

        return riskTypes;
    }

    private RiskEvent.RiskLevel calculateRiskLevel(double anomalyScore,
                                                   List<RiskEvent.RiskType> riskTypes) {
        int scoreLevel = 0;
        if (anomalyScore >= 0.9) scoreLevel = 4;
        else if (anomalyScore >= 0.7) scoreLevel = 3;
        else if (anomalyScore >= 0.5) scoreLevel = 2;
        else scoreLevel = 1;

        int riskTypeLevel = riskTypes.stream()
            .mapToInt(rt -> switch (rt) {
                case HIGH_RISK_SCOPE, PAYMENT_WRITE -> 3;
                case NEW_APPLICATION, UNUSUAL_TIME, RAPID_SUCCESSION -> 2;
                default -> 1;
            })
            .max()
            .orElse(1);

        int maxLevel = Math.max(scoreLevel, riskTypeLevel);

        return switch (maxLevel) {
            case 4 -> RiskEvent.RiskLevel.CRITICAL;
            case 3 -> RiskEvent.RiskLevel.HIGH;
            case 2 -> RiskEvent.RiskLevel.MEDIUM;
            default -> RiskEvent.RiskLevel.LOW;
        };
    }

    private RiskEvent.RiskType determinePrimaryRiskType(List<RiskEvent.RiskType> riskTypes) {
        if (riskTypes.isEmpty()) {
            return RiskEvent.RiskType.UNUSUAL_TIME;
        }

        return riskTypes.stream()
            .max(Comparator.comparingInt(rt -> switch (rt) {
                case HIGH_RISK_SCOPE -> 4;
                case NEW_APPLICATION -> 3;
                case UNUSUAL_TIME, RAPID_SUCCESSION -> 2;
                default -> 1;
            }))
            .orElse(riskTypes.get(0));
    }

    private String buildRiskReason(UserAuthorization authorization,
                                   AuthorizationFeature feature,
                                   List<RiskEvent.RiskType> riskTypes,
                                   double anomalyScore) {
        StringBuilder reason = new StringBuilder();

        if (riskTypes.contains(RiskEvent.RiskType.UNUSUAL_TIME)) {
            reason.append("授权时间为凌晨时段(23:00-05:00)。");
        }

        if (riskTypes.contains(RiskEvent.RiskType.NEW_APPLICATION)) {
            reason.append("用户首次授权该应用。");
        }

        if (riskTypes.contains(RiskEvent.RiskType.UNUSUAL_DURATION)) {
            reason.append("授权有效期为永久。");
        }

        if (riskTypes.contains(RiskEvent.RiskType.HIGH_RISK_SCOPE)) {
            reason.append("包含高危权限请求。");
        }

        if (riskTypes.contains(RiskEvent.RiskType.RAPID_SUCCESSION)) {
            reason.append(String.format("近7天内已有%d次授权。", feature.getAuthorizationCountLast7Days()));
        }

        if (anomalyScore >= mlConfig.getAnomalyDetection().getThreshold()) {
            reason.append(String.format("异常评分%.2f超过阈值%.2f。",
                anomalyScore, mlConfig.getAnomalyDetection().getThreshold()));
        }

        return reason.toString();
    }

    @Transactional
    public void confirmAsSelf(Long riskEventId) {
        RiskEvent event = riskEventRepository.findById(riskEventId)
            .orElseThrow(() -> new RuntimeException("Risk event not found"));
        event.setStatus(RiskEvent.EventStatus.CONFIRMED_SELF);
        event.setConfirmedAt(LocalDateTime.now());
        riskEventRepository.save(event);
        log.info("Risk event {} confirmed as self by user", riskEventId);
    }

    @Transactional
    public void revokeAndConfirm(Long riskEventId) {
        RiskEvent event = riskEventRepository.findById(riskEventId)
            .orElseThrow(() -> new RuntimeException("Risk event not found"));

        if (event.getAuthorization() != null) {
            event.getAuthorization().setActive(false);
            event.getAuthorization().setRevokedAt(LocalDateTime.now());
        }

        event.setStatus(RiskEvent.EventStatus.REVOKED);
        event.setResolvedAt(LocalDateTime.now());
        riskEventRepository.save(event);

        log.info("Risk event {} revoked by user", riskEventId);
    }

    @Transactional
    public void dismissEvent(Long riskEventId) {
        RiskEvent event = riskEventRepository.findById(riskEventId)
            .orElseThrow(() -> new RuntimeException("Risk event not found"));
        event.setStatus(RiskEvent.EventStatus.DISMISSED);
        event.setResolvedAt(LocalDateTime.now());
        riskEventRepository.save(event);
        log.info("Risk event {} dismissed", riskEventId);
    }

    public List<RiskEvent> getPendingRiskEvents() {
        return riskEventRepository.findByStatusOrderByDetectedAtDesc(RiskEvent.EventStatus.PENDING);
    }

    public List<RiskEvent> getUserRiskEvents(Long userId) {
        return riskEventRepository.findByUserIdOrderByDetectedAtDesc(userId);
    }

    public List<RiskEvent> getRecentRiskEvents() {
        return riskEventRepository.findTop100ByOrderByDetectedAtDesc();
    }

    public List<RiskEvent> getRiskEventsByLevel(RiskEvent.RiskLevel level) {
        return riskEventRepository.findByRiskLevelGreaterThanEqualOrderByDetectedAtDesc(level);
    }

    public Map<String, Object> getRiskStatistics() {
        List<RiskEvent> allEvents = riskEventRepository.findTop100ByOrderByDetectedAtDesc();

        Map<String, Long> levelCounts = new HashMap<>();
        Map<String, Long> typeCounts = new HashMap<>();
        Map<String, Long> statusCounts = new HashMap<>();

        for (RiskEvent event : allEvents) {
            levelCounts.merge(event.getRiskLevel().name(), 1L, Long::sum);
            typeCounts.merge(event.getRiskType().name(), 1L, Long::sum);
            statusCounts.merge(event.getStatus().name(), 1L, Long::sum);
        }

        return Map.of(
            "totalEvents", (long) allEvents.size(),
            "pendingCount", statusCounts.getOrDefault("PENDING", 0L),
            "byLevel", levelCounts,
            "byType", typeCounts,
            "byStatus", statusCounts,
            "modelReady", modelService.isModelReady(),
            "lastTrainingTime", modelService.getLastTrainingTime()
        );
    }
}
