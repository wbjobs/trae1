package com.loadtest.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import lombok.Builder;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "test_tasks")
@NoArgsConstructor
@AllArgsConstructor
@Builder
public class TestTask {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, length = 200)
    private String name;

    @Column(name = "config_id", nullable = false)
    private Long configId;

    @Column(nullable = false, length = 20)
    @Enumerated(EnumType.STRING)
    private TaskStatus status;

    @Column(name = "started_at")
    private LocalDateTime startedAt;

    @Column(name = "completed_at")
    private LocalDateTime completedAt;

    @Column(name = "created_at")
    private LocalDateTime createdAt;

    @Column(name = "updated_at")
    private LocalDateTime updatedAt;

    @Column(columnDefinition = "TEXT")
    private String resultSummary;

    @Column(name = "total_requests")
    private Long totalRequests;

    @Column(name = "success_count")
    private Long successCount;

    @Column(name = "failure_count")
    private Long failureCount;

    @Column(name = "avg_response_time")
    private Double avgResponseTime;

    @Column(name = "min_response_time")
    private Double minResponseTime;

    @Column(name = "max_response_time")
    private Double maxResponseTime;

    @Column(name = "p95_response_time")
    private Double p95ResponseTime;

    @Column(name = "p99_response_time")
    private Double p99ResponseTime;

    @Column(name = "throughput")
    private Double throughput;

    @Column(name = "error_rate")
    private Double errorRate;

    @Column(length = 1000)
    private String jmxPath;

    @Column(length = 1000)
    private String resultPath;

    @Column(length = 1000)
    private String reportPath;

    @Column(nullable = false)
    @Enumerated(EnumType.STRING)
    private TaskPriority priority = TaskPriority.MEDIUM;

    @PrePersist
    protected void onCreate() {
        createdAt = LocalDateTime.now();
        updatedAt = LocalDateTime.now();
        if (status == null) {
            status = TaskStatus.PENDING;
        }
    }

    @PreUpdate
    protected void onUpdate() {
        updatedAt = LocalDateTime.now();
    }

    public enum TaskStatus {
        PENDING,
        RUNNING,
        COMPLETED,
        FAILED,
        STOPPED
    }

    public enum TaskPriority {
        LOW,
        MEDIUM,
        HIGH,
        CRITICAL
    }
}
