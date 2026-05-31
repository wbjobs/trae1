package com.oauth2.audit.service;

import com.oauth2.audit.entity.AuditLog;
import com.oauth2.audit.repository.AuditLogRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.time.LocalTime;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Service
@RequiredArgsConstructor
public class AuditLogService {
    private final AuditLogRepository auditLogRepository;

    public AuditLog logRequest(String userId, String clientId, String clientName, String username,
                               String scope, String resourcePath, String httpMethod, Integer httpStatus) {
        AuditLog log = new AuditLog();
        log.setUserId(userId);
        log.setClientId(clientId);
        log.setClientName(clientName);
        log.setUsername(username);
        log.setScope(scope);
        log.setResourcePath(resourcePath);
        log.setHttpMethod(httpMethod);
        log.setTimestamp(LocalDateTime.now());
        log.setHttpStatus(httpStatus);

        detectAnomaly(log);

        return auditLogRepository.save(log);
    }

    private void detectAnomaly(AuditLog log) {
        LocalTime time = log.getTimestamp().toLocalTime();
        boolean isLateNight = time.isAfter(LocalTime.of(23, 0)) || time.isBefore(LocalTime.of(5, 0));

        if (isLateNight) {
            log.setIsAnomaly(true);
            log.setAnomalyType("LATE_NIGHT_CALL");
        } else {
            log.setIsAnomaly(false);
        }
    }

    public List<AuditLog> getLogsByUserId(String userId) {
        return auditLogRepository.findByUserIdOrderByTimestampDesc(userId);
    }

    public List<AuditLog> getLogsByClientId(String clientId) {
        return auditLogRepository.findByClientIdOrderByTimestampDesc(clientId);
    }

    public List<AuditLog> getLogsByTimeRange(LocalDateTime start, LocalDateTime end) {
        return auditLogRepository.findByTimestampBetweenOrderByTimestampDesc(start, end);
    }

    public List<AuditLog> getAnomalyLogs() {
        return auditLogRepository.findByIsAnomalyTrueOrderByTimestampDesc();
    }

    public Map<String, Object> getDashboardStats() {
        Map<String, Object> stats = new HashMap<>();
        
        List<AuditLog> allLogs = (List<AuditLog>) auditLogRepository.findAll();
        
        Map<String, Long> clientCallCount = new HashMap<>();
        Map<String, Long> userCallCount = new HashMap<>();
        
        for (AuditLog log : allLogs) {
            clientCallCount.merge(log.getClientId(), 1L, Long::sum);
            userCallCount.merge(log.getUserId(), 1L, Long::sum);
        }
        
        stats.put("topClients", clientCallCount);
        stats.put("topUsers", userCallCount);
        stats.put("totalCalls", allLogs.size());
        stats.put("anomalyCount", allLogs.stream().filter(AuditLog::getIsAnomaly).count());
        
        return stats;
    }
}
