package com.oauth2.audit.repository;

import com.oauth2.audit.entity.AuditLog;
import org.springframework.data.elasticsearch.repository.ElasticsearchRepository;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface AuditLogRepository extends ElasticsearchRepository<AuditLog, String> {
    List<AuditLog> findByUserIdOrderByTimestampDesc(String userId);
    List<AuditLog> findByClientIdOrderByTimestampDesc(String clientId);
    List<AuditLog> findByTimestampBetweenOrderByTimestampDesc(LocalDateTime start, LocalDateTime end);
    List<AuditLog> findByUserIdAndClientIdAndTimestampBetweenOrderByTimestampDesc(
            String userId, String clientId, LocalDateTime start, LocalDateTime end);
    List<AuditLog> findByIsAnomalyTrueOrderByTimestampDesc();
}
