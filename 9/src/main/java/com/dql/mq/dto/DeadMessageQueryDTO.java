package com.dql.mq.dto;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class DeadMessageQueryDTO {

    private String queueName;

    private String status;

    private String messageId;

    private LocalDateTime startTime;

    private LocalDateTime endTime;

    @Builder.Default
    private Integer page = 0;

    @Builder.Default
    private Integer size = 20;
}
