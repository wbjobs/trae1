package com.dql.mq.entity;

import jakarta.persistence.*;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Entity
@Table(name = "t_archived_message")
@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ArchivedMessage {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "message_id", nullable = false)
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

    @Column(name = "retry_count")
    private Integer retryCount;

    @Column(name = "final_status", length = 32)
    private String finalStatus;

    @Column(name = "archived_time", nullable = false)
    @Builder.Default
    private LocalDateTime archivedTime = LocalDateTime.now();

    @Column(name = "original_created_time")
    private LocalDateTime originalCreatedTime;

    @Column(name = "archive_reason", length = 200)
    private String archiveReason;
}
