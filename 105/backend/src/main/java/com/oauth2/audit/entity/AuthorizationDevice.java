package com.oauth2.audit.entity;

import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;

@Entity
@Table(name = "authorization_devices")
@Data
public class AuthorizationDevice {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "authorization_id")
    private UserAuthorization authorization;

    @Column(length = 512, nullable = false)
    private String deviceFingerprint;

    @Column(length = 128)
    private String deviceName;

    @Column(length = 64)
    private String userAgent;

    @Column(length = 45)
    private String ipAddress;

    @Column(nullable = false)
    private LocalDateTime lastActiveAt;

    @Column(nullable = false)
    private Boolean current = false;
}
