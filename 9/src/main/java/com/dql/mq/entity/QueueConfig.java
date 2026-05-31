package com.dql.mq.entity;

import jakarta.persistence.*;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Entity
@Table(name = "t_queue_config")
@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueConfig {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "queue_name", nullable = false, unique = true, length = 255)
    private String queueName;

    @Column(name = "exchange_name", nullable = false, length = 255)
    private String exchangeName;

    @Column(name = "routing_key", nullable = false, length = 255)
    private String routingKey;

    @Column(name = "dlq_exchange_name", length = 255)
    private String dlqExchangeName;

    @Column(name = "dlq_routing_key", length = 255)
    private String dlqRoutingKey;

    @Column(name = "dlq_queue_name", length = 255)
    private String dlqQueueName;

    @Column(name = "max_retry_count", nullable = false)
    @Builder.Default
    private Integer maxRetryCount = 3;

    @Column(name = "retry_interval", nullable = false)
    @Builder.Default
    private Long retryInterval = 5000L;

    @Column(name = "enabled", nullable = false)
    @Builder.Default
    private Boolean enabled = true;

    @Column(name = "auto_dlq", nullable = false)
    @Builder.Default
    private Boolean autoDlq = true;

    @Column(name = "acknowledge_mode", length = 32)
    @Builder.Default
    private String acknowledgeMode = "MANUAL";

    @Column(name = "prefetch_count")
    @Builder.Default
    private Integer prefetchCount = 10;

    @Column(name = "concurrency")
    @Builder.Default
    private Integer concurrency = 3;

    @Column(name = "max_concurrency")
    @Builder.Default
    private Integer maxConcurrency = 10;

    @Column(name = "created_time", nullable = false)
    @Builder.Default
    private LocalDateTime createdTime = LocalDateTime.now();

    @Column(name = "updated_time")
    private LocalDateTime updatedTime;

    @Column(name = "description", length = 500)
    private String description;
}
