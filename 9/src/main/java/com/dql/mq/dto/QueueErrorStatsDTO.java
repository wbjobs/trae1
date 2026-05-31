package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueErrorStatsDTO {

    private String queueName;

    private Long totalErrorCount;

    private Long failedCount;

    private Long deadLetterCount;

    private Long retryingCount;

    private Long successCount;

    private Double errorRate;

    private String topCategory;

    private String topErrorType;
}
