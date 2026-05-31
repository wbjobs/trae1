package com.oauth2.audit.repository;

import com.oauth2.audit.entity.RiskEvent;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.List;

@Repository
public interface RiskEventRepository extends JpaRepository<RiskEvent, Long> {
    List<RiskEvent> findByUserIdOrderByDetectedAtDesc(Long userId);
    List<RiskEvent> findByStatusOrderByDetectedAtDesc(RiskEvent.EventStatus status);
    List<RiskEvent> findByDetectedAtBetweenOrderByDetectedAtDesc(LocalDateTime start, LocalDateTime end);
    List<RiskEvent> findByUserIdAndStatusOrderByDetectedAtDesc(Long userId, RiskEvent.EventStatus status);
    List<RiskEvent> findByRiskLevelGreaterThanEqualOrderByDetectedAtDesc(RiskEvent.RiskLevel level);
    List<RiskEvent> findTop100ByOrderByDetectedAtDesc();
}
