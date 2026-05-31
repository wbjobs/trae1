package com.dql.mq.entity;

import jakarta.persistence.*;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Entity
@Table(name = "t_dead_message")
@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class DeadMessage {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "message_id", nullable = false, unique = true)
    private String messageId;

    @Column(name = "queue_name", nullable = false, length = 255)
    private String queueName;

    @Column(name = "exchange_name", length = 255)
    private String exchangeName;

    @Column(name = "routing_key", length = 255)
    private String routingKey;

    @Column(name = "message_body", columnDefinition = "TEXT")
    private String messageBody;

    @Column(name = "headers", columnDefinition = "TEXT")
    private String headers;

    @Column(name = "error_message", columnDefinition = "TEXT")
    private String errorMessage;

    @Column(name = "error_stack", columnDefinition = "TEXT")
    private String errorStack;

    @Column(name = "retry_count", nullable = false)
    @Builder.Default
    private Integer retryCount = 0;

    @Column(name = "max_retry_count", nullable = false)
    @Builder.Default
    private Integer maxRetryCount = 3;

    @Column(name = "status", nullable = false, length = 32)
    @Builder.Default
    private String status = "PENDING";

    @Column(name = "created_time", nullable = false)
    @Builder.Default
    private LocalDateTime createdTime = LocalDateTime.now();

    @Column(name = "updated_time")
    private LocalDateTime updatedTime;

    @Column(name = "next_retry_time")
    private LocalDateTime nextRetryTime;

    @Column(name = "original_created_time")
    private LocalDateTime originalCreatedTime;

    @Column(name = "consumer", length = 255)
    private String consumer;

    @Column(name = "remark", length = 500)
    private String remark;

    @Column(name = "category", length = 64)
    @Builder.Default
    private String category = "SYSTEM";

    @Column(name = "error_type", length = 64)
    private String errorType;

    @Column(name = "error_code", length = 64)
    private String errorCode;
}
