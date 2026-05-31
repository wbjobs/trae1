package com.dql.mq.handler;

import com.dql.mq.entity.DeadMessage;
import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.DeadMessageRepository;
import com.dql.mq.repository.QueueConfigRepository;
import com.dql.mq.util.MessageCategoryUtil;
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

@Slf4j
@Component
@RequiredArgsConstructor
public class MessageHandler {

    private final DeadMessageRepository deadMessageRepository;
    private final QueueConfigRepository queueConfigRepository;

    public void handleMessage(Message message, Channel channel) throws Exception {
        MessageProperties properties = message.getMessageProperties();
        String messageId = properties.getMessageId();
        String consumerQueue = properties.getConsumerQueue();
        long deliveryTag = properties.getDeliveryTag();

        if (messageId == null || messageId.isEmpty()) {
            messageId = java.util.UUID.randomUUID().toString();
        }

        try {
            processBusinessLogic(message);

            channel.basicAck(deliveryTag, false);

            Optional<DeadMessage> existingOpt = deadMessageRepository.findByMessageId(messageId);
            if (existingOpt.isPresent()) {
                DeadMessage deadMessage = existingOpt.get();
                if ("RETRYING".equals(deadMessage.getStatus()) || "PENDING".equals(deadMessage.getStatus())) {
                    deadMessage.setStatus("SUCCESS");
                    deadMessage.setUpdatedTime(LocalDateTime.now());
                    deadMessageRepository.save(deadMessage);
                    log.info("消息重试成功，更新状态为SUCCESS: messageId={}", messageId);
                }
            }

            log.info("消息处理成功: messageId={}, queue={}", messageId, consumerQueue);

        } catch (Exception e) {
            log.error("消息处理失败: messageId={}, queue={}, error={}", messageId, consumerQueue, e.getMessage());

            try {
                Optional<QueueConfig> configOpt = queueConfigRepository.findByQueueName(consumerQueue);
                int maxRetryCount = 3;
                long retryInterval = 5000;
                if (configOpt.isPresent()) {
                    if (configOpt.get().getMaxRetryCount() != null) {
                        maxRetryCount = configOpt.get().getMaxRetryCount();
                    }
                    if (configOpt.get().getRetryInterval() != null) {
                        retryInterval = configOpt.get().getRetryInterval();
                    }
                }

                Optional<DeadMessage> existingOpt = deadMessageRepository.findByMessageId(messageId);

                if (existingOpt.isPresent()) {
                    DeadMessage deadMessage = existingOpt.get();

                    if (deadMessage.getRetryCount() >= maxRetryCount) {
                        deadMessage.setStatus("FAILED");
                        deadMessage.setErrorMessage(e.getMessage());
                        deadMessage.setErrorStack(getStackTrace(e));
                        deadMessage.setErrorType(MessageCategoryUtil.determineErrorType(e.getMessage(), getStackTrace(e)));
                        deadMessage.setErrorCode(MessageCategoryUtil.determineErrorCode(deadMessage.getErrorType(), e.getMessage()));
                        deadMessage.setUpdatedTime(LocalDateTime.now());
                        deadMessageRepository.save(deadMessage);

                        channel.basicReject(deliveryTag, false);
                        log.warn("消息重试次数已达上限，转入死信队列: messageId={}, retryCount={}/{}",
                                messageId, deadMessage.getRetryCount(), maxRetryCount);
                    } else {
                        deadMessage.setRetryCount(deadMessage.getRetryCount() + 1);
                        deadMessage.setErrorMessage(e.getMessage());
                        deadMessage.setErrorStack(getStackTrace(e));
                        deadMessage.setErrorType(MessageCategoryUtil.determineErrorType(e.getMessage(), getStackTrace(e)));
                        deadMessage.setErrorCode(MessageCategoryUtil.determineErrorCode(deadMessage.getErrorType(), e.getMessage()));
                        deadMessage.setStatus("RETRYING");
                        deadMessage.setUpdatedTime(LocalDateTime.now());
                        deadMessage.setNextRetryTime(LocalDateTime.now().plusNanos(retryInterval * 1000000));
                        deadMessageRepository.save(deadMessage);

                        channel.basicAck(deliveryTag, false);
                        log.info("消息记录重试状态，由定时任务负责重试: messageId={}, retryCount={}/{}",
                                messageId, deadMessage.getRetryCount(), maxRetryCount);
                    }
                } else {
                    String exchangeName = properties.getReceivedExchange();
                    String routingKey = properties.getReceivedRoutingKey();

                    if ((exchangeName == null || exchangeName.isEmpty()) && configOpt.isPresent()) {
                        exchangeName = configOpt.get().getExchangeName();
                        routingKey = configOpt.get().getRoutingKey();
                    }

                    String errorType = MessageCategoryUtil.determineErrorType(e.getMessage(), getStackTrace(e));
                    String category = MessageCategoryUtil.categorizeMessage(consumerQueue, e.getMessage(), getStackTrace(e));

                    DeadMessage deadMessage = DeadMessage.builder()
                            .messageId(messageId)
                            .queueName(consumerQueue)
                            .exchangeName(exchangeName)
                            .routingKey(routingKey)
                            .messageBody(new String(message.getBody(), "UTF-8"))
                            .headers(convertHeadersToString(properties.getHeaders()))
                            .errorMessage(e.getMessage())
                            .errorStack(getStackTrace(e))
                            .retryCount(1)
                            .maxRetryCount(maxRetryCount)
                            .status("RETRYING")
                            .category(category)
                            .errorType(errorType)
                            .errorCode(MessageCategoryUtil.determineErrorCode(errorType, e.getMessage()))
                            .originalCreatedTime(LocalDateTime.now())
                            .nextRetryTime(LocalDateTime.now().plusNanos(retryInterval * 1000000))
                            .build();

                    deadMessageRepository.save(deadMessage);

                    channel.basicAck(deliveryTag, false);
                    log.info("消息首次失败，已记录准备重试: messageId={}, queue={}, category={}", messageId, consumerQueue, category);
                }

            } catch (Exception ex) {
                log.error("处理消息失败的异常捕获失败: messageId={}", messageId, ex);
                try {
                    channel.basicReject(deliveryTag, false);
                } catch (Exception ex1) {
                    log.error("消息拒绝失败: messageId={}", messageId, ex1);
                }
            }
        }
    }

    private void processBusinessLogic(Message message) {
        log.debug("处理业务逻辑: messageId={}", message.getMessageProperties().getMessageId());
    }

    private String getStackTrace(Exception e) {
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        e.printStackTrace(pw);
        return sw.toString();
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
}
