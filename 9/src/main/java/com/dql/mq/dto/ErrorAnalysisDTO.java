package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.List;
import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ErrorAnalysisDTO {

    private Long totalErrorCount;

    private Long totalDeadLetterCount;

    private Long totalFailedCount;

    private Long totalRetryingCount;

    private Long totalSuccessCount;

    private List<CategoryStatsDTO> categoryStats;

    private List<ErrorTypeStatsDTO> errorTypeStats;

    private List<ErrorCodeStatsDTO> errorCodeStats;

    private List<ErrorMessageStatsDTO> topErrorMessages;

    private List<QueueErrorStatsDTO> queueErrorStats;

    private Map<String, Long> retryableErrorCount;

    private Map<String, Long> nonRetryableErrorCount;

    private Double averageRetryCount;

    private Long totalRetryCount;

    private String analysisTime;
}
