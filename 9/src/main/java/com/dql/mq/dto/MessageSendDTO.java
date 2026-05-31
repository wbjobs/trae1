package com.dql.mq.dto;

import jakarta.validation.constraints.NotBlank;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class MessageSendDTO {

    @NotBlank(message = "交换机名称不能为空")
    private String exchangeName;

    @NotBlank(message = "路由键不能为空")
    private String routingKey;

    @NotBlank(message = "消息体不能为空")
    private String messageBody;

    private Map<String, Object> headers;

    private String messageId;

    private Boolean persistent;
}
