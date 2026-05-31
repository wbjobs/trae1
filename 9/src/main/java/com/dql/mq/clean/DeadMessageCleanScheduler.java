package com.dql.mq.clean;

import com.dql.mq.entity.ArchivedMessage;
import com.dql.mq.entity.DeadMessage;
import com.dql.mq.repository.ArchivedMessageRepository;
import com.dql.mq.repository.DeadMessageRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.List;

@Slf4j
@Component
@RequiredArgsConstructor
public class DeadMessageCleanScheduler {

    private final DeadMessageRepository deadMessageRepository;
    private final ArchivedMessageRepository archivedMessageRepository;

    @Value("${mq.dql.clean-enabled:true}")
    private boolean cleanEnabled;

    @Value("${mq.dql.failed-clean-threshold-days:30}")
    private int failedCleanThresholdDays;

    @Value("${mq.dql.success-clean-threshold-days:7}")
    private int successCleanThresholdDays;

    @Value("${mq.dql.dead-letter-clean-threshold-days:15}")
    private int deadLetterCleanThresholdDays;

    @Value("${mq.dql.batch-size:100}")
    private int batchSize;

    @Value("${mq.dql.auto-archive-before-clean:true}")
    private boolean autoArchiveBeforeClean;

    @Scheduled(cron = "0 30 2 * * ?")
    public void autoCleanSuccessMessages() {
        if (!cleanEnabled) {
            log.debug("自动清理已禁用");
            return;
        }

        log.info("开始自动清理成功的死信消息...");

        LocalDateTime thresholdTime = LocalDateTime.now().minusDays(successCleanThresholdDays);
        int totalCleaned = 0;

        while (true) {
            Pageable pageable = PageRequest.of(0, batchSize);
            Page<DeadMessage> page = deadMessageRepository.findByStatusAndCreatedTimeBefore(
                    "SUCCESS", thresholdTime, pageable);

            List<DeadMessage> messages = page.getContent();
            if (messages.isEmpty()) {
                break;
            }

            for (DeadMessage message : messages) {
                try {
                    if (autoArchiveBeforeClean) {
                        ArchivedMessage archived = ArchivedMessage.builder()
                                .messageId(message.getMessageId())
                                .queueName(message.getQueueName())
                                .exchangeName(message.getExchangeName())
                                .routingKey(message.getRoutingKey())
                                .messageBody(message.getMessageBody())
                                .headers(message.getHeaders())
                                .errorMessage(message.getErrorMessage())
                                .retryCount(message.getRetryCount())
                                .finalStatus(message.getStatus())
                                .originalCreatedTime(message.getOriginalCreatedTime())
                                .archiveReason("自动清理归档: 成功消息超过" + successCleanThresholdDays + "天")
                                .build();

                        archivedMessageRepository.save(archived);
                    }

                    deadMessageRepository.deleteById(message.getId());
                    totalCleaned++;
                } catch (Exception e) {
                    log.error("清理消息失败: messageId={}", message.getMessageId(), e);
                }
            }

            if (totalCleaned >= batchSize * 10) {
                log.info("清理数量已达上限，停止本次清理");
                break;
            }
        }

        log.info("自动清理成功消息完成，共清理 {} 条消息", totalCleaned);
    }

    @Scheduled(cron = "0 0 3 * * ?")
    public void autoCleanFailedMessages() {
        if (!cleanEnabled) {
            log.debug("自动清理失败消息已禁用");
            return;
        }

        log.info("开始自动清理失败的死信消息...");

        LocalDateTime thresholdTime = LocalDateTime.now().minusDays(failedCleanThresholdDays);
        int totalCleaned = 0;

        while (true) {
            Pageable pageable = PageRequest.of(0, batchSize);
            Page<DeadMessage> page = deadMessageRepository.findByStatusAndCreatedTimeBefore(
                    "FAILED", thresholdTime, pageable);

            List<DeadMessage> messages = page.getContent();
            if (messages.isEmpty()) {
                break;
            }

            for (DeadMessage message : messages) {
                try {
                    if (autoArchiveBeforeClean) {
                        ArchivedMessage archived = ArchivedMessage.builder()
                                .messageId(message.getMessageId())
                                .queueName(message.getQueueName())
                                .exchangeName(message.getExchangeName())
                                .routingKey(message.getRoutingKey())
                                .messageBody(message.getMessageBody())
                                .headers(message.getHeaders())
                                .errorMessage(message.getErrorMessage())
                                .retryCount(message.getRetryCount())
                                .finalStatus(message.getStatus())
                                .originalCreatedTime(message.getOriginalCreatedTime())
                                .archiveReason("自动清理归档: 失败消息超过" + failedCleanThresholdDays + "天")
                                .build();

                        archivedMessageRepository.save(archived);
                    }

                    deadMessageRepository.deleteById(message.getId());
                    totalCleaned++;
                } catch (Exception e) {
                    log.error("清理失败消息失败: messageId={}", message.getMessageId(), e);
                }
            }

            if (totalCleaned >= batchSize * 10) {
                log.info("清理数量已达上限，停止本次清理");
                break;
            }
        }

        log.info("自动清理失败消息完成，共清理 {} 条消息", totalCleaned);
    }

    @Scheduled(cron = "0 30 3 * * ?")
    public void autoCleanDeadLetterMessages() {
        if (!cleanEnabled) {
            log.debug("自动清理死信消息已禁用");
            return;
        }

        log.info("开始自动清理死信队列中的消息...");

        LocalDateTime thresholdTime = LocalDateTime.now().minusDays(deadLetterCleanThresholdDays);
        int totalCleaned = 0;

        while (true) {
            Pageable pageable = PageRequest.of(0, batchSize);
            Page<DeadMessage> page = deadMessageRepository.findByStatusAndCreatedTimeBefore(
                    "DEAD_LETTER", thresholdTime, pageable);

            List<DeadMessage> messages = page.getContent();
            if (messages.isEmpty()) {
                break;
            }

            for (DeadMessage message : messages) {
                try {
                    if (autoArchiveBeforeClean) {
                        ArchivedMessage archived = ArchivedMessage.builder()
                                .messageId(message.getMessageId())
                                .queueName(message.getQueueName())
                                .exchangeName(message.getExchangeName())
                                .routingKey(message.getRoutingKey())
                                .messageBody(message.getMessageBody())
                                .headers(message.getHeaders())
                                .errorMessage(message.getErrorMessage())
                                .retryCount(message.getRetryCount())
                                .finalStatus(message.getStatus())
                                .originalCreatedTime(message.getOriginalCreatedTime())
                                .archiveReason("自动清理归档: 死信消息超过" + deadLetterCleanThresholdDays + "天")
                                .build();

                        archivedMessageRepository.save(archived);
                    }

                    deadMessageRepository.deleteById(message.getId());
                    totalCleaned++;
                } catch (Exception e) {
                    log.error("清理死信消息失败: messageId={}", message.getMessageId(), e);
                }
            }

            if (totalCleaned >= batchSize * 10) {
                log.info("清理数量已达上限，停止本次清理");
                break;
            }
        }

        log.info("自动清理死信消息完成，共清理 {} 条消息", totalCleaned);
    }
}
