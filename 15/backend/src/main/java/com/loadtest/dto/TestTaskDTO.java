package com.loadtest.dto;

import lombok.Data;
import lombok.NoArgsConstructor;
import lombok.AllArgsConstructor;
import lombok.Builder;

@Data
@NoArgsConstructor
@AllArgsConstructor
@Builder
public class TestTaskDTO {
    private Long id;
    private String name;
    private Long configId;
    private String status;
    private String startedAt;
    private String completedAt;
    private String createdAt;
    private String updatedAt;
    private Long totalRequests;
    private Long successCount;
    private Long failureCount;
    private Double avgResponseTime;
    private Double minResponseTime;
    private Double maxResponseTime;
    private Double p95ResponseTime;
    private Double p99ResponseTime;
    private Double throughput;
    private Double errorRate;
    private String resultSummary;
    private String priority;
}
