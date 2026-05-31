package com.oauth2.audit.entity;

import jakarta.persistence.*;
import lombok.Data;
import java.time.LocalDateTime;

@Entity
@Table(name = "risk_events")
@Data
public class RiskEvent {
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "user_id", nullable = false)
    private User user;

    @ManyToOne(fetch = FetchType.LAZY)
    @JoinColumn(name = "authorization_id")
    private UserAuthorization authorization;

    @Column(nullable = false)
    private Double anomalyScore;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false)
    private RiskLevel riskLevel;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false)
    private RiskType riskType;

    @Column(length = 512)
    private String riskReason;

    @Column(columnDefinition = "TEXT")
    private String featureVector;

    @Column(nullable = false)
    private LocalDateTime detectedAt;

    @Enumerated(EnumType.STRING)
    @Column(nullable = false)
    private EventStatus status = EventStatus.PENDING;

    private LocalDateTime confirmedAt;
    private LocalDateTime resolvedAt;

    @Column(length = 45)
    private String notifyPhone;

    @Column(length = 128)
    private String notifyEmail;

    @Column(nullable = false)
    private Boolean notificationSent = false;

    private LocalDateTime notificationSentAt;

    public enum RiskLevel {
        LOW(1),
        MEDIUM(2),
        HIGH(3),
        CRITICAL(4);

        private final int severity;

        RiskLevel(int severity) {
            this.severity = severity;
        }

        public int getSeverity() {
            return severity;
        }
    }

    public enum RiskType {
        UNUSUAL_TIME("异常时间授权"),
        NEW_APPLICATION("新应用授权"),
        UNUSUAL_LOCATION("异常位置授权"),
        UNUSUAL_DURATION("异常有效期授权"),
        HIGH_RISK_SCOPE("高危权限授权"),
        RAPID_SUCCESSION("短时间内多次授权"),
        NEVER_USED_APP_TYPE("未使用过的应用类型");

        private final String displayName;

        RiskType(String displayName) {
            this.displayName = displayName;
        }

        public String getDisplayName() {
            return displayName;
        }
    }

    public enum EventStatus {
        PENDING("待确认"),
        CONFIRMED_SELF("本人操作"),
        REVOKED("已撤销"),
        DISMISSED("已忽略");

        private final String displayName;

        EventStatus(String displayName) {
            this.displayName = displayName;
        }

        public String getDisplayName() {
            return displayName;
        }
    }
}
