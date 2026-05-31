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
public class DeadMessageDTO {

    private Long id;

    private String messageId;

    private String queueName;

    private String exchangeName;

    private String routingKey;

    private String messageBody;

    private String headers;

    private String errorMessage;

    private Integer retryCount;

    private Integer maxRetryCount;

    private String status;

    private LocalDateTime createdTime;

    private LocalDateTime updatedTime;

    private LocalDateTime nextRetryTime;

    private LocalDateTime originalCreatedTime;

    private String consumer;

    private String remark;

    private String category;

    private String errorType;

    private String errorCode;
}
