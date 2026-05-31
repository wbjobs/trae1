package com.oauth2.audit.controller;

import com.oauth2.audit.entity.UserAuthorization;
import com.oauth2.audit.service.AuthorizationService;
import lombok.Data;
import lombok.RequiredArgsConstructor;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Set;

@RestController
@RequestMapping("/api/authorizations")
@RequiredArgsConstructor
@CrossOrigin(origins = "*")
public class AuthorizationController {
    private final AuthorizationService authorizationService;

    @PostMapping
    public ResponseEntity<UserAuthorization> createAuthorization(@RequestBody CreateAuthorizationRequest request) {
        UserAuthorization authorization = authorizationService.createAuthorization(
                request.getUserId(),
                request.getClientId(),
                request.getScopes(),
                request.getDuration(),
                request.getIpAddress(),
                request.getDeviceFingerprint(),
                request.getDeviceName(),
                request.getUserAgent()
        );
        return ResponseEntity.ok(authorization);
    }

    @DeleteMapping("/{authorizationId}")
    public ResponseEntity<Void> revokeAuthorization(@PathVariable Long authorizationId) {
        authorizationService.revokeAuthorization(authorizationId);
        return ResponseEntity.ok().build();
    }

    @DeleteMapping("/user/{userId}/all")
    public ResponseEntity<Void> revokeAllUserAuthorizations(@PathVariable Long userId) {
        authorizationService.revokeAllUserAuthorizations(userId);
        return ResponseEntity.ok().build();
    }

    @DeleteMapping("/user/{userId}/device/{deviceFingerprint}")
    public ResponseEntity<Void> revokeUserDeviceAuthorizations(
            @PathVariable Long userId,
            @PathVariable String deviceFingerprint) {
        authorizationService.revokeAuthorizationByDevice(userId, deviceFingerprint);
        return ResponseEntity.ok().build();
    }

    @GetMapping("/user/{userId}")
    public ResponseEntity<List<UserAuthorization>> getUserAuthorizations(@PathVariable Long userId) {
        return ResponseEntity.ok(authorizationService.getUserAuthorizations(userId));
    }

    @GetMapping("/user/{userId}/history")
    public ResponseEntity<List<UserAuthorization>> getUserAuthorizationHistory(@PathVariable Long userId) {
        return ResponseEntity.ok(authorizationService.getAllUserAuthorizations(userId));
    }

    @Data
    public static class CreateAuthorizationRequest {
        private Long userId;
        private Long clientId;
        private Set<String> scopes;
        private UserAuthorization.AuthorizationDuration duration;
        private String ipAddress;
        private String deviceFingerprint;
        private String deviceName;
        private String userAgent;
    }
}
