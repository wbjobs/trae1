package com.dql.mq.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class QueueConfigDTO {

    private Long id;

    @NotBlank(message = "队列名称不能为空")
    @Size(max = 255, message = "队列名称长度不能超过255")
    private String queueName;

    @NotBlank(message = "交换机名称不能为空")
    @Size(max = 255, message = "交换机名称长度不能超过255")
    private String exchangeName;

    @NotBlank(message = "路由键不能为空")
    @Size(max = 255, message = "路由键长度不能超过255")
    private String routingKey;

    @Size(max = 255, message = "死信交换机名称长度不能超过255")
    private String dlqExchangeName;

    @Size(max = 255, message = "死信路由键长度不能超过255")
    private String dlqRoutingKey;

    @Size(max = 255, message = "死信队列名称长度不能超过255")
    private String dlqQueueName;

    @Builder.Default
    private Integer maxRetryCount = 3;

    @Builder.Default
    private Long retryInterval = 5000L;

    @Builder.Default
    private Boolean enabled = true;

    @Builder.Default
    private Boolean autoDlq = true;

    @Builder.Default
    private String acknowledgeMode = "MANUAL";

    @Builder.Default
    private Integer prefetchCount = 10;

    @Builder.Default
    private Integer concurrency = 3;

    @Builder.Default
    private Integer maxConcurrency = 10;

    @Size(max = 500, message = "描述长度不能超过500")
    private String description;
}
