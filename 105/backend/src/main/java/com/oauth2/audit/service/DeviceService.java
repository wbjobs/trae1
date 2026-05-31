package com.oauth2.audit.service;

import com.oauth2.audit.entity.AuthorizationDevice;
import com.oauth2.audit.entity.UserAuthorization;
import com.oauth2.audit.repository.AuthorizationDeviceRepository;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Set;

@Service
@RequiredArgsConstructor
public class DeviceService {
    private final AuthorizationDeviceRepository deviceRepository;

    @Transactional
    public AuthorizationDevice registerDevice(UserAuthorization authorization, String deviceFingerprint,
                                               String deviceName, String userAgent, String ipAddress) {
        AuthorizationDevice device = new AuthorizationDevice();
        device.setAuthorization(authorization);
        device.setDeviceFingerprint(deviceFingerprint);
        device.setDeviceName(deviceName != null ? deviceName : parseDeviceName(userAgent));
        device.setUserAgent(userAgent);
        device.setIpAddress(ipAddress);
        device.setLastActiveAt(LocalDateTime.now());
        device.setCurrent(true);

        markOtherDevicesAsNotCurrent(authorization.getId());

        return deviceRepository.save(device);
    }

    private void markOtherDevicesAsNotCurrent(Long authorizationId) {
        List<AuthorizationDevice> devices = deviceRepository.findByAuthorizationId(authorizationId);
        for (AuthorizationDevice device : devices) {
            device.setCurrent(false);
        }
        deviceRepository.saveAll(devices);
    }

    @Transactional
    public void updateDeviceActivity(String deviceFingerprint) {
        List<AuthorizationDevice> devices = deviceRepository.findByDeviceFingerprint(deviceFingerprint);
        for (AuthorizationDevice device : devices) {
            device.setLastActiveAt(LocalDateTime.now());
        }
        deviceRepository.saveAll(devices);
    }

    public List<AuthorizationDevice> getDevicesByAuthorization(Long authorizationId) {
        return deviceRepository.findByAuthorizationId(authorizationId);
    }

    public String parseDeviceName(String userAgent) {
        if (userAgent == null || userAgent.isEmpty()) {
            return "Unknown Device";
        }

        if (userAgent.contains("Windows")) {
            return "Windows PC";
        } else if (userAgent.contains("Mac OS")) {
            return "Mac";
        } else if (userAgent.contains("Linux")) {
            return "Linux PC";
        } else if (userAgent.contains("iPhone")) {
            return "iPhone";
        } else if (userAgent.contains("iPad")) {
            return "iPad";
        } else if (userAgent.contains("Android")) {
            return "Android Device";
        }

        return "Unknown Device";
    }
}
