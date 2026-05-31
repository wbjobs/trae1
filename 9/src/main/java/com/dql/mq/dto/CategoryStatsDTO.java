package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class CategoryStatsDTO {

    private String category;

    private String categoryName;

    private Long count;

    private Long successCount;

    private Long failedCount;

    private Long retryingCount;

    private Long deadLetterCount;
}
