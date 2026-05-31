package com.oauth2.audit.entity;

import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;

@Entity
@Table(name = "authorization_features")
@Data
public class AuthorizationFeature {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "user_id", nullable = false)
    private User user;

    @Column(nullable = false)
    private LocalDateTime authorizationTime;

    @Column(nullable = false)
    private Integer hourOfDay;

    @Column(nullable = false)
    private Integer dayOfWeek;

    @Column(nullable = false)
    private Integer clientId;

    @Column(length = 64)
    private String clientType;

    @Column(nullable = false)
    private Integer scopeCount;

    @Column(nullable = false)
    private Boolean hasHighRiskScope;

    @Column(nullable = false)
    private Integer durationHours;

    @Column(nullable = false)
    private String ipAddress;

    @Column(length = 128)
    private String deviceType;

    @Column
    private Double latitude;

    @Column
    private Double longitude;

    @Column
    private Integer timeSinceLastAuthorization;

    @Column
    private Integer authorizationCountLast7Days;

    @Column
    private Integer authorizationCountLast30Days;

    @Column
    private Boolean isNewApplication;

    @Column
    private Boolean isNewDevice;

    @Column(columnDefinition = "TEXT")
    private String featureVector;

    @Column(nullable = false)
    private LocalDateTime createdAt;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "related_authorization_id")
    private UserAuthorization relatedAuthorization;
}
