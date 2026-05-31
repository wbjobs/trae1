package com.oauth2.audit.controller;

import com.oauth2.audit.entity.AuditLog;
import com.oauth2.audit.service.AuditLogService;
import lombok.RequiredArgsConstructor;
import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/audit")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class AuditController {
    private final AuditLogService auditLogService;

    @GetMapping("/logs/user/{userId}")
    public ResponseEntity<List<AuditLog>> getLogsByUser(@PathVariable String userId) {
        return ResponseEntity.ok(auditLogService.getLogsByUserId(userId));
    }

    @GetMapping("/logs/client/{clientId}")
    public ResponseEntity<List<AuditLog>> getLogsByClient(@PathVariable String clientId) {
        return ResponseEntity.ok(auditLogService.getLogsByClientId(clientId));
    }

    @GetMapping("/logs/range")
    public ResponseEntity<List<AuditLog>> getLogsByTimeRange(
            @RequestParam @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime start,
            @RequestParam @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime end) {
        return ResponseEntity.ok(auditLogService.getLogsByTimeRange(start, end));
    }

    @GetMapping("/logs/anomalies")
    public ResponseEntity<List<AuditLog>> getAnomalyLogs() {
        return ResponseEntity.ok(auditLogService.getAnomalyLogs());
    }

    @GetMapping("/dashboard/stats")
    public ResponseEntity<Map<String, Object>> getDashboardStats() {
        return ResponseEntity.ok(auditLogService.getDashboardStats());
    }
}
