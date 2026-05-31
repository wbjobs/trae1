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
public class ArchivedMessageDTO {

    private Long id;

    private String messageId;

    private String queueName;

    private String exchangeName;

    private String routingKey;

    private String messageBody;

    private String errorMessage;

    private Integer retryCount;

    private String finalStatus;

    private LocalDateTime archivedTime;

    private LocalDateTime originalCreatedTime;

    private String archiveReason;
}
