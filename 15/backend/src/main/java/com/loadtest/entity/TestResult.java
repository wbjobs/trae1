package com.loadtest.entity;

import jakarta.persistence.*;
import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import lombok.Builder;
import java.time.LocalDateTime;

@Data
@Entity
@Table(name = "test_results")
@NoArgsConstructor
@AllArgsConstructor
@Builder
public class TestResult {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "task_id", nullable = false)
    private Long taskId;

    @Column(name = "sample_label", length = 200)
    private String sampleLabel;

    @Column(name = "timestamp")
    private LocalDateTime timestamp;

    @Column(name = "elapsed")
    private Long elapsed;

    @Column(name = "response_code", length = 10)
    private String responseCode;

    @Column(name = "response_message", length = 500)
    private String responseMessage;

    @Column(name = "success")
    private Boolean success;

    @Column(name = "bytes")
    private Long bytes;

    @Column(name = "sent_bytes")
    private Long sentBytes;

    @Column(name = "grp_threads")
    private Integer grpThreads;

    @Column(name = "all_threads")
    private Integer allThreads;

    @Column(name = "url", length = 1000)
    private String url;

    @Column(name = "latency")
    private Long latency;

    @Column(name = "idle_time")
    private Long idleTime;

    @Column(name = "connect")
    private Long connect;

    @Column(name = "created_at")
    private LocalDateTime createdAt;

    @PrePersist
    protected void onCreate() {
        createdAt = LocalDateTime.now();
    }
}
