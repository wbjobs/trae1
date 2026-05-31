package com.dql.mq.retry;

import com.dql.mq.entity.DeadMessage;
import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.DeadMessageRepository;
import com.dql.mq.repository.QueueConfigRepository;
import com.dql.mq.util.MessageCategoryUtil;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.rabbit.core.RabbitTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Map;
import java.util.Optional;

@Slf4j
@Component
@RequiredArgsConstructor
public class AutoRetryScheduler {

    private final DeadMessageRepository deadMessageRepository;
    private final QueueConfigRepository queueConfigRepository;
    private final RabbitTemplate rabbitTemplate;

    @Scheduled(fixedDelay = 10000, initialDelay = 30000)
    public void autoRetryMessages() {
        log.debug("开始自动重试任务...");

        List<DeadMessage> retryMessages = deadMessageRepository.findByStatusAndNextRetryTimeBefore(
                "RETRYING", LocalDateTime.now());

        if (retryMessages.isEmpty()) {
            log.debug("没有需要自动重试的消息");
            return;
        }

        log.info("找到 {} 条需要自动重试的消息", retryMessages.size());

        int successCount = 0;
        int failCount = 0;
        int skipCount = 0;

        for (DeadMessage message : retryMessages) {
            try {
                int maxRetryCount = message.getMaxRetryCount() != null ? message.getMaxRetryCount() : 3;

                if (message.getRetryCount() >= maxRetryCount) {
                    message.setStatus("FAILED");
                    message.setUpdatedTime(LocalDateTime.now());
                    deadMessageRepository.save(message);
                    log.warn("消息已达到最大重试次数，标记为失败: messageId={}", message.getMessageId());
                    continue;
                }

                String errorType = message.getErrorType();
                boolean isRetriable = MessageCategoryUtil.isRetriableError(errorType, message.getCategory());

                if (!isRetriable && message.getRetryCount() >= 1) {
                    message.setStatus("FAILED");
                    message.setErrorMessage("不可重试的错误类型，停止重试: " + errorType);
                    message.setUpdatedTime(LocalDateTime.now());
                    deadMessageRepository.save(message);
                    log.warn("消息错误类型不可重试，跳过重试: messageId={}, errorType={}",
                            message.getMessageId(), errorType);
                    skipCount++;
                    continue;
                }

                String exchangeName = message.getExchangeName();
                String routingKey = message.getRoutingKey();

                if (exchangeName == null || exchangeName.isEmpty() || routingKey == null || routingKey.isEmpty()) {
                    Optional<QueueConfig> configOpt = queueConfigRepository.findByQueueName(message.getQueueName());
                    if (configOpt.isPresent()) {
                        QueueConfig config = configOpt.get();
                        if (exchangeName == null || exchangeName.isEmpty()) {
                            exchangeName = config.getExchangeName();
                        }
                        if (routingKey == null || routingKey.isEmpty()) {
                            routingKey = config.getRoutingKey();
                        }
                    }
                }

                if (exchangeName == null || exchangeName.isEmpty() || routingKey == null || routingKey.isEmpty()) {
                    log.error("无法获取交换机或路由键，跳过重试: messageId={}, exchangeName={}, routingKey={}",
                            message.getMessageId(), exchangeName, routingKey);
                    message.setStatus("FAILED");
                    message.setErrorMessage("交换机或路由键为空，无法重试");
                    message.setErrorType(MessageCategoryUtil.ERROR_TYPE_SYSTEM);
                    message.setErrorCode("SYS_002");
                    message.setUpdatedTime(LocalDateTime.now());
                    deadMessageRepository.save(message);
                    failCount++;
                    continue;
                }

                Optional<QueueConfig> configOpt = queueConfigRepository.findByQueueName(message.getQueueName());
                long retryInterval = 5000;
                if (configOpt.isPresent() && configOpt.get().getRetryInterval() != null) {
                    retryInterval = configOpt.get().getRetryInterval();
                }

                if (errorType != null && MessageCategoryUtil.ERROR_TYPE_TIMEOUT.equals(errorType)) {
                    retryInterval = Math.min(retryInterval * 2, 60000);
                }

                final String finalExchangeName = exchangeName;
                final String finalRoutingKey = routingKey;
                final String originalMessageId = message.getMessageId();
                final long finalRetryInterval = retryInterval;

                Map<String, Object> headers = new java.util.HashMap<>();
                headers.put("x-retry-count", message.getRetryCount() + 1);
                headers.put("x-original-message-id", message.getMessageId());
                headers.put("x-error-type", errorType);
                headers.put("x-error-code", message.getErrorCode());

                rabbitTemplate.convertAndSend(
                        finalExchangeName,
                        finalRoutingKey,
                        message.getMessageBody(),
                        msg -> {
                            msg.getMessageProperties().setMessageId(originalMessageId);
                            msg.getMessageProperties().setHeaders(headers);
                            return msg;
                        }
                );

                message.setRetryCount(message.getRetryCount() + 1);
                message.setStatus("RETRYING");
                message.setUpdatedTime(LocalDateTime.now());
                message.setNextRetryTime(LocalDateTime.now().plusNanos(finalRetryInterval * 1000000));
                deadMessageRepository.save(message);

                successCount++;
                log.info("自动重试成功: messageId={}, exchange={}, routingKey={}, retryCount={}, retryInterval={}ms",
                        message.getMessageId(), finalExchangeName, finalRoutingKey, message.getRetryCount(), finalRetryInterval);

            } catch (Exception e) {
                failCount++;
                log.error("自动重试失败: messageId={}", message.getMessageId(), e);

                try {
                    String errorType = MessageCategoryUtil.determineErrorType(e.getMessage(), null);
                    String errorCode = MessageCategoryUtil.determineErrorCode(errorType, e.getMessage());

                    message.setErrorType(errorType);
                    message.setErrorCode(errorCode);
                    message.setErrorMessage(e.getMessage());

                    if (!MessageCategoryUtil.isRetriableError(errorType, message.getCategory())) {
                        message.setStatus("FAILED");
                        message.setUpdatedTime(LocalDateTime.now());
                        deadMessageRepository.save(message);
                        log.warn("重试失败且错误类型不可重试，标记为失败: messageId={}, errorType={}",
                                message.getMessageId(), errorType);
                    } else {
                        int maxRetryCount = message.getMaxRetryCount() != null ? message.getMaxRetryCount() : 3;
                        if (message.getRetryCount() >= maxRetryCount) {
                            message.setStatus("FAILED");
                            message.setUpdatedTime(LocalDateTime.now());
                            deadMessageRepository.save(message);
                        }
                    }
                } catch (Exception ex) {
                    log.error("更新消息状态失败: messageId={}", message.getMessageId(), ex);
                }
            }
        }

        log.info("自动重试任务完成，成功: {}, 失败: {}, 跳过: {}", successCount, failCount, skipCount);
    }
}
