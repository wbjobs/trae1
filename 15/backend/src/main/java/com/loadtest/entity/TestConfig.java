package com.loadtest.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import lombok.Builder;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "test_configs")
@NoArgsConstructor
@AllArgsConstructor
@Builder
public class TestConfig {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, length = 200)
    private String name;

    @Column(length = 500)
    private String description;

    @Column(nullable = false, length = 10)
    @Enumerated(EnumType.STRING)
    private HttpMethod method;

    @Column(nullable = false, length = 1000)
    private String url;

    @Column(columnDefinition = "TEXT")
    private String headers;

    @Column(columnDefinition = "TEXT")
    private String requestBody;

    @Column(nullable = false)
    private Integer threadCount;

    @Column(nullable = false)
    private Integer rampUpTime;

    @Column(nullable = false)
    private Integer loopCount;

    @Column(nullable = false)
    private Integer duration;

    @Column(nullable = false)
    private Boolean useLoopCount;

    private String protocol;

    private Integer port;

    private String path;

    @Column(length = 100)
    private String domain;

    @Column(name = "simulate_delay")
    private Boolean simulateDelay = false;

    @Column(name = "delay_min_ms")
    private Integer delayMinMs;

    @Column(name = "delay_max_ms")
    private Integer delayMaxMs;

    @Column(name = "simulate_timeout")
    private Boolean simulateTimeout = false;

    @Column(name = "timeout_probability")
    private Double timeoutProbability;

    @Column(name = "simulate_error")
    private Boolean simulateError = false;

    @Column(name = "error_probability")
    private Double errorProbability;

    @Column(name = "error_status_codes")
    private String errorStatusCodes;

    @Column(name = "connection_timeout")
    private Integer connectionTimeout = 10000;

    @Column(name = "response_timeout")
    private Integer responseTimeout = 30000;

    @Column(name = "created_at")
    private LocalDateTime createdAt;

    @Column(name = "updated_at")
    private LocalDateTime updatedAt;

    @PrePersist
    protected void onCreate() {
        createdAt = LocalDateTime.now();
        updatedAt = LocalDateTime.now();
    }

    @PreUpdate
    protected void onUpdate() {
        updatedAt = LocalDateTime.now();
    }

    public enum HttpMethod {
        GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS
    }
}
