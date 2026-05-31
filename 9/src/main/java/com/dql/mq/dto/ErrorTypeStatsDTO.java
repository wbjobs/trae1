package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ErrorTypeStatsDTO {

    private String errorType;

    private String errorTypeName;

    private Long count;

    private Long queueCount;

    private Long successCount;

    private Long failedCount;
}
