package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueOpsStatsDTO {

    private LocalDateTime statsTime;

    private Long totalQueueCount;

    private Long activeQueueCount;

    private Long disabledQueueCount;

    private Long totalDeadMessageCount;

    private Long totalArchivedMessageCount;

    private Long totalSuccessMessageCount;

    private Long totalFailedMessageCount;

    private Long totalRetryingMessageCount;

    private Long totalDeadLetterCount;

    private Double overallSuccessRate;

    private Double overallFailureRate;

    private Map<String, Boolean> listenerStatus;

    private List<QueueDetailStatsDTO> queueDetails;

    private List<CategoryStatsDTO> categoryStats;

    private List<ErrorTypeStatsDTO> errorTypeStats;
}
