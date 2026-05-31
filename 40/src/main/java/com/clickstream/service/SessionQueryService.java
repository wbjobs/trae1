package com.clickstream.service;

import com.clickstream.model.Session;
import com.clickstream.model.SessionDetail;
import com.clickstream.store.SessionStore;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;

import java.util.List;
import java.util.stream.Collectors;

@Service
public class SessionQueryService {

    private final SessionStore sessionStore;

    @Autowired
    public SessionQueryService(SessionStore sessionStore) {
        this.sessionStore = sessionStore;
    }

    public SessionDetail getSessionDetail(String sessionId) {
        return sessionStore.findBySessionId(sessionId);
    }

    public List<Session> getUserSessions(String userId, int days) {
        return sessionStore.findByUserIdWithinDays(userId, days).stream()
                .map(this::toSession)
                .collect(Collectors.toList());
    }

    public List<SessionDetail> getUserSessionDetails(String userId, int days) {
        return sessionStore.findByUserIdWithinDays(userId, days);
    }

    public List<Session> getUserRecentSessions(String userId) {
        return getUserSessions(userId, 7);
    }

    public List<SessionDetail> getUserRecentSessionDetails(String userId) {
        return getUserSessionDetails(userId, 7);
    }

    public int getTotalSessionCount() {
        return sessionStore.getSessionCount();
    }

    private Session toSession(SessionDetail detail) {
        return Session.builder()
                .sessionId(detail.getSessionId())
                .userId(detail.getUserId())
                .startTime(detail.getStartTime())
                .endTime(detail.getEndTime())
                .durationSeconds(detail.getDurationSeconds())
                .pageCount(detail.getPageCount())
                .bounce(detail.isBounce())
                .userAgent(detail.getUserAgent())
                .build();
    }
}
