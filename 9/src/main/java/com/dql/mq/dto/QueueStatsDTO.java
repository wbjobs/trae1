package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueStatsDTO {

    private String queueName;

    private Long pendingCount;

    private Long retryingCount;

    private Long successCount;

    private Long failedCount;

    private Long deadLetterCount;

    private Long archivedCount;

    private Long totalCount;
}
