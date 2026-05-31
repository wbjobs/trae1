package com.dql.mq.service.impl;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.MessageSendDTO;
import com.dql.mq.service.MessageSendService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.core.Message;
import org.springframework.amqp.core.MessageBuilder;
import org.springframework.amqp.core.MessageProperties;
import org.springframework.amqp.rabbit.core.RabbitTemplate;
import org.springframework.stereotype.Service;

import java.nio.charset.StandardCharsets;
import java.util.Map;
import java.util.UUID;

@Slf4j
@Service
@RequiredArgsConstructor
public class MessageSendServiceImpl implements MessageSendService {

    private final RabbitTemplate rabbitTemplate;

    @Override
    public ApiResponse<String> sendMessage(MessageSendDTO dto) {
        try {
            String messageId = dto.getMessageId() != null && !dto.getMessageId().isEmpty()
                    ? dto.getMessageId()
                    : UUID.randomUUID().toString();

            MessageProperties messageProperties = new MessageProperties();
            messageProperties.setMessageId(messageId);
            messageProperties.setContentType(MessageProperties.CONTENT_TYPE_JSON);

            if (dto.getHeaders() != null && !dto.getHeaders().isEmpty()) {
                for (Map.Entry<String, Object> entry : dto.getHeaders().entrySet()) {
                    messageProperties.setHeader(entry.getKey(), entry.getValue());
                }
            }

            if (dto.getPersistent() != null && dto.getPersistent()) {
                messageProperties.setDeliveryMode(MessageProperties.DEFAULT_DELIVERY_MODE);
            }

            Message message = MessageBuilder
                    .withBody(dto.getMessageBody().getBytes(StandardCharsets.UTF_8))
                    .andProperties(messageProperties)
                    .build();

            rabbitTemplate.send(dto.getExchangeName(), dto.getRoutingKey(), message);

            log.info("消息发送成功: exchange={}, routingKey={}, messageId={}",
                    dto.getExchangeName(), dto.getRoutingKey(), messageId);

            return ApiResponse.success("发送成功", messageId);

        } catch (Exception e) {
            log.error("消息发送失败", e);
            return ApiResponse.error("发送失败: " + e.getMessage());
        }
    }

    @Override
    public ApiResponse<String> sendDelayMessage(MessageSendDTO dto, Long delayMs) {
        try {
            String messageId = dto.getMessageId() != null && !dto.getMessageId().isEmpty()
                    ? dto.getMessageId()
                    : UUID.randomUUID().toString();

            MessageProperties messageProperties = new MessageProperties();
            messageProperties.setMessageId(messageId);
            messageProperties.setContentType(MessageProperties.CONTENT_TYPE_JSON);
            messageProperties.setDelay(delayMs.intValue());

            if (dto.getHeaders() != null && !dto.getHeaders().isEmpty()) {
                for (Map.Entry<String, Object> entry : dto.getHeaders().entrySet()) {
                    messageProperties.setHeader(entry.getKey(), entry.getValue());
                }
            }

            Message message = MessageBuilder
                    .withBody(dto.getMessageBody().getBytes(StandardCharsets.UTF_8))
                    .andProperties(messageProperties)
                    .build();

            rabbitTemplate.send(dto.getExchangeName(), dto.getRoutingKey(), message);

            log.info("延迟消息发送成功: exchange={}, routingKey={}, messageId={}, delayMs={}",
                    dto.getExchangeName(), dto.getRoutingKey(), messageId, delayMs);

            return ApiResponse.success("发送成功", messageId);

        } catch (Exception e) {
            log.error("延迟消息发送失败", e);
            return ApiResponse.error("发送失败: " + e.getMessage());
        }
    }
}
