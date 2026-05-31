package com.oauth2.audit.controller;

import com.oauth2.audit.entity.AuditLog;
import com.oauth2.audit.service.AuditLogService;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.ArrayList;
import java.util.List;
import java.util.Random;

@RestController
@RequestMapping("/api/test")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class TestController {
    private final AuditLogService auditLogService;
    private final Random random = new Random();

    private static final String[] USERS = {"1", "2", "3"};
    private static final String[] USERNAMES = {"admin", "user1", "user2"};
    private static final String[] CLIENTS = {"demo-client", "analytics-app"};
    private static final String[] CLIENT_NAMES = {"Demo Application", "Analytics Dashboard"};
    private static final String[] SCOPES = {"profile", "read", "write"};
    private static final String[] PATHS = {"/api/resource/profile", "/api/resource/data"};
    private static final String[] METHODS = {"GET", "POST", "PUT", "DELETE"};

    @PostMapping("/generate-logs")
    public List<AuditLog> generateTestLogs(@RequestParam(defaultValue = "50") int count) {
        List<AuditLog> logs = new ArrayList<>();
        for (int i = 0; i < count; i++) {
            int userIndex = random.nextInt(USERS.length);
            int clientIndex = random.nextInt(CLIENTS.length);
            
            AuditLog log = auditLogService.logRequest(
                USERS[userIndex],
                CLIENTS[clientIndex],
                CLIENT_NAMES[clientIndex],
                USERNAMES[userIndex],
                SCOPES[random.nextInt(SCOPES.length)],
                PATHS[random.nextInt(PATHS.length)],
                METHODS[random.nextInt(METHODS.length)],
                200
            );
            logs.add(log);
        }
        return logs;
    }
}
