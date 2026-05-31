package com.oauth2.audit.service;

import com.oauth2.audit.entity.ClientApplication;
import com.oauth2.audit.entity.User;
import com.oauth2.audit.entity.UserAuthorization;
import com.oauth2.audit.repository.ClientApplicationRepository;
import com.oauth2.audit.repository.UserAuthorizationRepository;
import com.oauth2.audit.repository.UserRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Set;

@Service
@RequiredArgsConstructor
public class AuthorizationService {
    private final UserAuthorizationRepository authorizationRepository;
    private final UserRepository userRepository;
    private final ClientApplicationRepository clientApplicationRepository;
    private final AnomalyDetectionService anomalyDetectionService;

    @Transactional
    public UserAuthorization createAuthorization(Long userId, Long clientId, Set<String> scopes,
                                                UserAuthorization.AuthorizationDuration duration,
                                                String ipAddress, String deviceFingerprint,
                                                String deviceName, String userAgent) {
        User user = userRepository.findById(userId)
                .orElseThrow(() -> new RuntimeException("User not found"));
        ClientApplication client = clientApplicationRepository.findById(clientId)
                .orElseThrow(() -> new RuntimeException("Client not found"));

        UserAuthorization authorization = new UserAuthorization();
        authorization.setUser(user);
        authorization.setClientApplication(client);
        authorization.setScopes(scopes);
        authorization.setDuration(duration);
        authorization.setAuthorizedAt(LocalDateTime.now());
        authorization.setActive(true);
        authorization.setIpAddress(ipAddress);
        authorization.setDeviceFingerprint(deviceFingerprint);
        authorization.setDeviceName(deviceName);
        authorization.setUserAgent(userAgent);

        if (duration == UserAuthorization.AuthorizationDuration.PERMANENT) {
            authorization.setExpiresAt(LocalDateTime.now().plusYears(100));
        } else {
            authorization.setExpiresAt(LocalDateTime.now().plusHours(duration.getHours()));
        }

        authorization = authorizationRepository.save(authorization);

        anomalyDetectionService.detectAnomaly(authorization);

        return authorization;
    }

    @Transactional
    public void revokeAuthorization(Long authorizationId) {
        UserAuthorization authorization = authorizationRepository.findById(authorizationId)
                .orElseThrow(() -> new RuntimeException("Authorization not found"));
        authorization.setActive(false);
        authorization.setRevokedAt(LocalDateTime.now());
        authorizationRepository.save(authorization);
    }

    @Transactional
    public void revokeAllUserAuthorizations(Long userId) {
        List<UserAuthorization> authorizations = authorizationRepository.findByUserId(userId);
        for (UserAuthorization auth : authorizations) {
            if (auth.getActive()) {
                auth.setActive(false);
                auth.setRevokedAt(LocalDateTime.now());
            }
        }
        authorizationRepository.saveAll(authorizations);
    }

    @Transactional
    public void revokeAuthorizationByDevice(Long userId, String deviceFingerprint) {
        List<UserAuthorization> authorizations = authorizationRepository.findByUserId(userId);
        for (UserAuthorization auth : authorizations) {
            if (auth.getActive() && deviceFingerprint.equals(auth.getDeviceFingerprint())) {
                auth.setActive(false);
                auth.setRevokedAt(LocalDateTime.now());
            }
        }
        authorizationRepository.saveAll(authorizations);
    }

    public List<UserAuthorization> getUserAuthorizations(Long userId) {
        return authorizationRepository.findByUserIdAndActiveTrue(userId);
    }

    public List<UserAuthorization> getAllUserAuthorizations(Long userId) {
        return authorizationRepository.findByUserId(userId);
    }

    public boolean isAuthorized(Long userId, Long clientId) {
        return authorizationRepository.findByUserIdAndClientApplicationIdAndActiveTrue(userId, clientId)
                .map(auth -> auth.getExpiresAt().isAfter(LocalDateTime.now()))
                .orElse(false);
    }
}
