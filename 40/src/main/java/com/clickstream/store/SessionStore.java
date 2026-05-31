package com.clickstream.store;

import com.clickstream.model.SessionDetail;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;

@Component
public class SessionStore {

    private static final Logger logger = LoggerFactory.getLogger(SessionStore.class);

    private final Map<String, SessionDetail> sessionStore = new ConcurrentHashMap<>();

    private static final int DATA_RETENTION_DAYS = 7;

    public void save(SessionDetail sessionDetail) {
        if (sessionDetail != null && sessionDetail.getSessionId() != null) {
            sessionStore.put(sessionDetail.getSessionId(), sessionDetail);
            logger.debug("Saved session: {}", sessionDetail.getSessionId());
        }
    }

    public SessionDetail findBySessionId(String sessionId) {
        return sessionStore.get(sessionId);
    }

    public List<SessionDetail> findByUserId(String userId) {
        return sessionStore.values().stream()
                .filter(session -> userId.equals(session.getUserId()))
                .sorted((a, b) -> b.getStartTime().compareTo(a.getStartTime()))
                .collect(Collectors.toList());
    }

    public List<SessionDetail> findByUserIdWithinDays(String userId, int days) {
        Instant threshold = Instant.now().minus(days, ChronoUnit.DAYS);
        return sessionStore.values().stream()
                .filter(session -> userId.equals(session.getUserId()))
                .filter(session -> session.getStartTime() != null && session.getStartTime().isAfter(threshold))
                .sorted((a, b) -> b.getStartTime().compareTo(a.getStartTime()))
                .collect(Collectors.toList());
    }

    public List<SessionDetail> findAll() {
        return new ArrayList<>(sessionStore.values());
    }

    public int getSessionCount() {
        return sessionStore.size();
    }

    @Scheduled(fixedRate = 3600000)
    public void cleanExpiredSessions() {
        Instant threshold = Instant.now().minus(DATA_RETENTION_DAYS, ChronoUnit.DAYS);
        int beforeSize = sessionStore.size();
        
        sessionStore.entrySet().removeIf(entry -> {
            SessionDetail session = entry.getValue();
            return session.getStartTime() != null && session.getStartTime().isBefore(threshold);
        });
        
        int removedCount = beforeSize - sessionStore.size();
        if (removedCount > 0) {
            logger.info("Cleaned {} expired sessions older than {} days", removedCount, DATA_RETENTION_DAYS);
        }
    }
}
