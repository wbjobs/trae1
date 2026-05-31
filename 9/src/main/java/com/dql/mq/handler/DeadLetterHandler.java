package com.dql.mq.handler;

import com.dql.mq.entity.DeadMessage;
import com.dql.mq.repository.DeadMessageRepository;
import com.rabbitmq.client.Channel;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.core.Message;
import org.springframework.amqp.core.MessageProperties;
import org.springframework.stereotype.Component;

import java.io.PrintWriter;
import java.io.StringWriter;
import java.time.LocalDateTime;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;

@Slf4j
@Component
@RequiredArgsConstructor
public class DeadLetterHandler {

    private final DeadMessageRepository deadMessageRepository;

    public void handleDeadLetter(Message message, Channel channel) throws Exception {
        MessageProperties properties = message.getMessageProperties();
        String messageId = properties.getMessageId();
        String consumerQueue = properties.getConsumerQueue();
        long deliveryTag = properties.getDeliveryTag();

        if (messageId == null || messageId.isEmpty()) {
            messageId = UUID.randomUUID().toString();
        }

        try {
            log.info("接收到死信消息: messageId={}, queue={}", messageId, consumerQueue);

            Optional<DeadMessage> existingOpt = deadMessageRepository.findByMessageId(messageId);

            if (existingOpt.isPresent()) {
                DeadMessage deadMessage = existingOpt.get();
                deadMessage.setStatus("DEAD_LETTER");
                deadMessage.setUpdatedTime(LocalDateTime.now());
                deadMessageRepository.save(deadMessage);
                log.info("更新死信消息状态: messageId={}", messageId);
            } else {
                String reason = getDeadLetterReason(properties);
                DeadMessage deadMessage = DeadMessage.builder()
                        .messageId(messageId)
                        .queueName(consumerQueue)
                        .exchangeName(properties.getReceivedExchange())
                        .routingKey(properties.getReceivedRoutingKey())
                        .messageBody(new String(message.getBody(), "UTF-8"))
                        .headers(convertHeadersToString(properties.getHeaders()))
                        .errorMessage("死信消息，原因: " + reason)
                        .retryCount(0)
                        .maxRetryCount(3)
                        .status("DEAD_LETTER")
                        .originalCreatedTime(LocalDateTime.now())
                        .build();

                deadMessageRepository.save(deadMessage);
                log.info("保存死信消息: messageId={}, queue={}", messageId, consumerQueue);
            }

            channel.basicAck(deliveryTag, false);
            log.info("死信消息处理确认: messageId={}", messageId);

        } catch (Exception e) {
            log.error("死信消息处理失败: messageId={}", messageId, e);
            try {
                channel.basicNack(deliveryTag, false, false);
            } catch (Exception ex) {
                log.error("死信消息拒绝失败: messageId={}", messageId, ex);
            }
        }
    }

    private String getDeadLetterReason(MessageProperties properties) {
        Map<String, Object> headers = properties.getHeaders();
        if (headers != null) {
            Object reason = headers.get("x-death");
            if (reason != null) {
                return reason.toString();
            }
        }
        return "未知原因";
    }

    private String convertHeadersToString(Map<String, Object> headers) {
        if (headers == null || headers.isEmpty()) {
            return null;
        }
        StringBuilder sb = new StringBuilder();
        headers.forEach((key, value) -> {
            sb.append(key).append("=").append(value).append(";");
        });
        return sb.toString();
    }

    private String getStackTrace(Exception e) {
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        e.printStackTrace(pw);
        return sw.toString();
    }
}
