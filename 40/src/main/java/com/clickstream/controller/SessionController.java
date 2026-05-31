package com.clickstream.controller;

import com.clickstream.model.Session;
import com.clickstream.model.SessionDetail;
import com.clickstream.service.SessionQueryService;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/sessions")
public class SessionController {

    private final SessionQueryService sessionQueryService;

    @Autowired
    public SessionController(SessionQueryService sessionQueryService) {
        this.sessionQueryService = sessionQueryService;
    }

    @GetMapping("/{sessionId}")
    public ResponseEntity<?> getSessionDetail(@PathVariable String sessionId) {
        SessionDetail detail = sessionQueryService.getSessionDetail(sessionId);
        if (detail == null) {
            Map<String, String> error = new HashMap<>();
            error.put("error", "Session not found");
            error.put("sessionId", sessionId);
            return ResponseEntity.notFound().build();
        }
        return ResponseEntity.ok(detail);
    }

    @GetMapping("/user/{userId}")
    public ResponseEntity<List<Session>> getUserSessions(
            @PathVariable String userId,
            @RequestParam(defaultValue = "7") int days) {
        List<Session> sessions = sessionQueryService.getUserSessions(userId, days);
        return ResponseEntity.ok(sessions);
    }

    @GetMapping("/user/{userId}/details")
    public ResponseEntity<List<SessionDetail>> getUserSessionDetails(
            @PathVariable String userId,
            @RequestParam(defaultValue = "7") int days) {
        List<SessionDetail> sessions = sessionQueryService.getUserSessionDetails(userId, days);
        return ResponseEntity.ok(sessions);
    }

    @GetMapping("/user/{userId}/recent")
    public ResponseEntity<List<Session>> getUserRecentSessions(@PathVariable String userId) {
        List<Session> sessions = sessionQueryService.getUserRecentSessions(userId);
        return ResponseEntity.ok(sessions);
    }

    @GetMapping("/user/{userId}/recent/details")
    public ResponseEntity<List<SessionDetail>> getUserRecentSessionDetails(@PathVariable String userId) {
        List<SessionDetail> sessions = sessionQueryService.getUserRecentSessionDetails(userId);
        return ResponseEntity.ok(sessions);
    }

    @GetMapping("/stats")
    public ResponseEntity<Map<String, Object>> getStats() {
        Map<String, Object> stats = new HashMap<>();
        stats.put("totalSessions", sessionQueryService.getTotalSessionCount());
        return ResponseEntity.ok(stats);
    }
}
