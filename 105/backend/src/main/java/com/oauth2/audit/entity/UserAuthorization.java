package com.oauth2.audit.entity;

import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;
import java.util.Set;

@Entity
@Table(name = "user_authorizations")
@Data
public class UserAuthorization {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "user_id", nullable = false)
    private User user;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "client_id", nullable = false)
    private ClientApplication clientApplication;

    @ElementCollection(fetch = FetchType.EAGER)
    @CollectionTable(name = "authorization_scopes", joinColumns = @JoinColumn(name = "authorization_id"))
    @Column(name = "scope")
    private Set<String> scopes;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false)
    private AuthorizationDuration duration;

    @Column(nullable = false)
    private LocalDateTime authorizedAt;

    @Column(nullable = false)
    private LocalDateTime expiresAt;

    @Column(nullable = false)
    private Boolean active = true;

    private LocalDateTime revokedAt;

    @Column(length = 45)
    private String ipAddress;

    @Column(length = 512)
    private String deviceFingerprint;

    @Column(length = 128)
    private String deviceName;

    @Column(length = 64)
    private String userAgent;

    @ElementCollection(fetch = FetchType.EAGER)
    @CollectionTable(name = "authorization_devices", joinColumns = @JoinColumn(name = "authorization_id"))
    private Set<AuthorizationDevice> devices;

    public enum AuthorizationDuration {
        ONE_HOUR(1),
        SEVEN_DAYS(7 * 24),
        PERMANENT(-1);

        private final int hours;

        AuthorizationDuration(int hours) {
            this.hours = hours;
        }

        public int getHours() {
            return hours;
        }
    }
}
