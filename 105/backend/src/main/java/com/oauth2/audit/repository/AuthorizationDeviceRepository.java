package com.oauth2.audit.repository;

import com.oauth2.audit.entity.AuthorizationDevice;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;

@Repository
public interface AuthorizationDeviceRepository extends JpaRepository<AuthorizationDevice, Long> {
    List<AuthorizationDevice> findByAuthorizationId(Long authorizationId);
    List<AuthorizationDevice> findByDeviceFingerprint(String deviceFingerprint);
}
