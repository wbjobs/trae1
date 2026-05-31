package com.oauth2.audit.service;

import com.oauth2.audit.entity.AuthorizationFeature;
import com.oauth2.audit.entity.UserAuthorization;
import com.oauth2.audit.repository.AuthorizationFeatureRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.stream.Collectors;

@Service
@RequiredArgsConstructor
@Slf4j
public class FeatureExtractionService {
    private final AuthorizationFeatureRepository featureRepository;

    private static final Map<String, String> CLIENT_TYPE_MAP = new HashMap<>();
    private static final Map<String, String> DEVICE_TYPE_MAP = new HashMap<>();

    static {
        CLIENT_TYPE_MAP.put("demo-client", "DEMO");
        CLIENT_TYPE_MAP.put("analytics-app", "ANALYTICS");
        CLIENT_TYPE_MAP.put("social-app", "SOCIAL");
        CLIENT_TYPE_MAP.put("finance-app", "FINANCE");

        DEVICE_TYPE_MAP.put("Windows PC", "WINDOWS");
        DEVICE_TYPE_MAP.put("Mac", "MACOS");
        DEVICE_TYPE_MAP.put("Linux PC", "LINUX");
        DEVICE_TYPE_MAP.put("iPhone", "IOS");
        DEVICE_TYPE_MAP.put("iPad", "IOS");
        DEVICE_TYPE_MAP.put("Android Device", "ANDROID");
    }

    @Transactional
    public AuthorizationFeature extractFeature(UserAuthorization authorization) {
        AuthorizationFeature feature = new AuthorizationFeature();

        feature.setUser(authorization.getUser());
        feature.setRelatedAuthorization(authorization);
        feature.setAuthorizationTime(authorization.getAuthorizedAt());
        feature.setCreatedAt(LocalDateTime.now());

        LocalDateTime authTime = authorization.getAuthorizedAt();
        feature.setHourOfDay(authTime.getHour());
        feature.setDayOfWeek(authTime.getDayOfWeek().getValue());

        feature.setClientId(authorization.getClientApplication().getId().intValue());
        feature.setClientType(CLIENT_TYPE_MAP.getOrDefault(
            authorization.getClientApplication().getClientId(), "OTHER"));

        feature.setScopeCount(authorization.getScopes() != null ? authorization.getScopes().size() : 0);
        feature.setHasHighRiskScope(authorization.getScopes() != null &&
            authorization.getScopes().stream().anyMatch(s ->
                s.contains("admin") || s.contains("finance") || s.contains("payment")));

        if (authorization.getDuration() != null) {
            if (authorization.getDuration() == UserAuthorization.AuthorizationDuration.PERMANENT) {
                feature.setDurationHours(-1);
            } else {
                feature.setDurationHours(authorization.getDuration().getHours());
            }
        } else {
            feature.setDurationHours(24 * 7);
        }

        feature.setIpAddress(authorization.getIpAddress() != null ? authorization.getIpAddress() : "0.0.0.0");
        feature.setDeviceType(DEVICE_TYPE_MAP.getOrDefault(authorization.getDeviceName(), "OTHER"));

        LocalDateTime sevenDaysAgo = LocalDateTime.now().minusDays(7);
        LocalDateTime thirtyDaysAgo = LocalDateTime.now().minusDays(30);

        List<AuthorizationFeature> recentFeatures = featureRepository.findByUserIdAndCreatedAtBetween(
            authorization.getUser().getId(), sevenDaysAgo, LocalDateTime.now());

        feature.setAuthorizationCountLast7Days(recentFeatures.size());

        List<AuthorizationFeature> monthFeatures = featureRepository.findByUserIdAndCreatedAtBetween(
            authorization.getUser().getId(), thirtyDaysAgo, LocalDateTime.now());

        feature.setAuthorizationCountLast30Days(monthFeatures.size());

        if (!recentFeatures.isEmpty()) {
            AuthorizationFeature lastFeature = recentFeatures.stream()
                .filter(f -> f.getRelatedAuthorization() != null &&
                    !f.getRelatedAuthorization().getId().equals(authorization.getId()))
                .max(Comparator.comparing(AuthorizationFeature::getAuthorizationTime))
                .orElse(null);

            if (lastFeature != null) {
                long hoursSince = ChronoUnit.HOURS.between(
                    lastFeature.getAuthorizationTime(), authorization.getAuthorizedAt());
                feature.setTimeSinceLastAuthorization((int) hoursSince);
            } else {
                feature.setTimeSinceLastAuthorization(0);
            }
        } else {
            feature.setTimeSinceLastAuthorization(Integer.MAX_VALUE);
        }

        Set<Integer> usedClientIds = recentFeatures.stream()
            .map(f -> f.getClientId())
            .collect(Collectors.toSet());
        feature.setIsNewApplication(!usedClientIds.contains(feature.getClientId()));

        Set<String> usedDeviceTypes = recentFeatures.stream()
            .map(AuthorizationFeature::getDeviceType)
            .collect(Collectors.toSet());
        feature.setIsNewDevice(!usedDeviceTypes.contains(feature.getDeviceType()));

        double[] featureVector = buildFeatureVector(feature);
        feature.setFeatureVector(vectorToString(featureVector));

        return featureRepository.save(feature);
    }

    public double[] buildFeatureVector(AuthorizationFeature feature) {
        double[] vector = new double[12];

        vector[0] = normalizeTimeFeature(feature.getHourOfDay(), 0, 23);
        vector[1] = normalizeTimeFeature(feature.getDayOfWeek(), 1, 7);
        vector[2] = normalizeCategorical(feature.getClientType());
        vector[3] = feature.getScopeCount() / 10.0;
        vector[4] = feature.getHasHighRiskScope() ? 1.0 : 0.0;
        vector[5] = normalizeDuration(feature.getDurationHours());
        vector[6] = normalizeCategorical(feature.getDeviceType());
        vector[7] = feature.getTimeSinceLastAuthorization() == Integer.MAX_VALUE ? 1.0 :
            Math.min(1.0, feature.getTimeSinceLastAuthorization() / 168.0);
        vector[8] = Math.min(1.0, feature.getAuthorizationCountLast7Days() / 20.0);
        vector[9] = Math.min(1.0, feature.getAuthorizationCountLast30Days() / 100.0);
        vector[10] = feature.getIsNewApplication() ? 1.0 : 0.0;
        vector[11] = feature.getIsNewDevice() ? 1.0 : 0.0;

        return vector;
    }

    public double[] buildFeatureVector(UserAuthorization authorization, AuthorizationFeature historicalFeature) {
        double[] vector = new double[12];

        LocalDateTime authTime = authorization.getAuthorizedAt();
        vector[0] = normalizeTimeFeature(authTime.getHour(), 0, 23);
        vector[1] = normalizeTimeFeature(authTime.getDayOfWeek().getValue(), 1, 7);
        vector[2] = normalizeCategorical(CLIENT_TYPE_MAP.getOrDefault(
            authorization.getClientApplication().getClientId(), "OTHER"));
        vector[3] = (authorization.getScopes() != null ? authorization.getScopes().size() : 0) / 10.0;
        vector[4] = (authorization.getScopes() != null &&
            authorization.getScopes().stream().anyMatch(s ->
                s.contains("admin") || s.contains("finance") || s.contains("payment"))) ? 1.0 : 0.0;

        if (authorization.getDuration() != null) {
            if (authorization.getDuration() == UserAuthorization.AuthorizationDuration.PERMANENT) {
                vector[5] = 1.0;
            } else {
                vector[5] = normalizeDuration(authorization.getDuration().getHours());
            }
        } else {
            vector[5] = 0.5;
        }

        vector[6] = normalizeCategorical(DEVICE_TYPE_MAP.getOrDefault(authorization.getDeviceName(), "OTHER"));

        if (historicalFeature != null) {
            vector[7] = historicalFeature.getTimeSinceLastAuthorization() == Integer.MAX_VALUE ? 1.0 :
                Math.min(1.0, historicalFeature.getTimeSinceLastAuthorization() / 168.0);
            vector[8] = Math.min(1.0, historicalFeature.getAuthorizationCountLast7Days() / 20.0);
            vector[9] = Math.min(1.0, historicalFeature.getAuthorizationCountLast30Days() / 100.0);
            vector[10] = historicalFeature.getIsNewApplication() ? 1.0 : 0.0;
            vector[11] = historicalFeature.getIsNewDevice() ? 1.0 : 0.0;
        } else {
            vector[7] = 1.0;
            vector[8] = 0.0;
            vector[9] = 0.0;
            vector[10] = 1.0;
            vector[11] = 1.0;
        }

        return vector;
    }

    private double normalizeTimeFeature(int value, int min, int max) {
        return (value - min) / (double) (max - min);
    }

    private double normalizeCategorical(String category) {
        return switch (category) {
            case "DEMO" -> 0.0;
            case "ANALYTICS" -> 0.2;
            case "SOCIAL" -> 0.4;
            case "FINANCE" -> 0.6;
            case "WINDOWS" -> 0.0;
            case "MACOS" -> 0.2;
            case "LINUX" -> 0.4;
            case "IOS" -> 0.6;
            case "ANDROID" -> 0.8;
            default -> 0.5;
        };
    }

    private double normalizeDuration(int hours) {
        if (hours < 0) return 1.0;
        return Math.min(1.0, hours / (24.0 * 30));
    }

    private String vectorToString(double[] vector) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < vector.length; i++) {
            if (i > 0) sb.append(",");
            sb.append(String.format("%.4f", vector[i]));
        }
        return sb.toString();
    }

    public List<double[]> extractFeatureVectorsForTraining(LocalDateTime since) {
        List<AuthorizationFeature> features = featureRepository.findAllSince(since);
        return features.stream()
            .filter(f -> f.getFeatureVector() != null)
            .map(f -> stringToVector(f.getFeatureVector()))
            .collect(Collectors.toList());
    }

    private double[] stringToVector(String vectorStr) {
        String[] parts = vectorStr.split(",");
        double[] vector = new double[parts.length];
        for (int i = 0; i < parts.length; i++) {
            vector[i] = Double.parseDouble(parts[i]);
        }
        return vector;
    }
}
