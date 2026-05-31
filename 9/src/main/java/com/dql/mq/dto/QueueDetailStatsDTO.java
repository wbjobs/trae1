package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueDetailStatsDTO {

    private String queueName;

    private String description;

    private Boolean enabled;

    private Integer maxRetryCount;

    private Long retryInterval;

    private Integer concurrency;

    private Integer maxConcurrency;

    private Long totalMessageCount;

    private Long successCount;

    private Long failedCount;

    private Long retryingCount;

    private Long deadLetterCount;

    private Long archivedCount;

    private Double successRate;

    private Double failureRate;

    private Boolean listenerRunning;

    private String topCategory;

    private String topErrorType;
}
