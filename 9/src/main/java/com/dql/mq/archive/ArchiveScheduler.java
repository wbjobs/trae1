package com.dql.mq.archive;

import com.dql.mq.entity.ArchivedMessage;
import com.dql.mq.entity.DeadMessage;
import com.dql.mq.repository.ArchivedMessageRepository;
import com.dql.mq.repository.DeadMessageRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.List;

@Slf4j
@Component
@RequiredArgsConstructor
public class ArchiveScheduler {

    private final DeadMessageRepository deadMessageRepository;
    private final ArchivedMessageRepository archivedMessageRepository;

    @Value("${mq.dql.archive-enabled:true}")
    private boolean archiveEnabled;

    @Value("${mq.dql.archive-threshold-days:7}")
    private int archiveThresholdDays;

    @Value("${mq.dql.batch-size:100}")
    private int batchSize;

    @Scheduled(cron = "0 0 2 * * ?")
    public void autoArchiveDeadMessages() {
        if (!archiveEnabled) {
            log.debug("自动归档已禁用");
            return;
        }

        log.info("开始自动归档死信消息...");

        List<String> statuses = List.of("SUCCESS", "FAILED");
        List<DeadMessage> deadMessages = deadMessageRepository.findByStatusIn(statuses);

        if (deadMessages.isEmpty()) {
            log.info("没有需要归档的死信消息");
            return;
        }

        int archivedCount = 0;
        for (DeadMessage deadMessage : deadMessages) {
            try {
                if (deadMessage.getCreatedTime().isAfter(LocalDateTime.now().minusDays(archiveThresholdDays))) {
                    continue;
                }

                ArchivedMessage archived = ArchivedMessage.builder()
                        .messageId(deadMessage.getMessageId())
                        .queueName(deadMessage.getQueueName())
                        .exchangeName(deadMessage.getExchangeName())
                        .routingKey(deadMessage.getRoutingKey())
                        .messageBody(deadMessage.getMessageBody())
                        .headers(deadMessage.getHeaders())
                        .errorMessage(deadMessage.getErrorMessage())
                        .retryCount(deadMessage.getRetryCount())
                        .finalStatus(deadMessage.getStatus())
                        .originalCreatedTime(deadMessage.getOriginalCreatedTime())
                        .archiveReason("自动归档")
                        .build();

                archivedMessageRepository.save(archived);
                deadMessageRepository.deleteById(deadMessage.getId());
                archivedCount++;

                if (archivedCount >= batchSize) {
                    break;
                }

            } catch (Exception e) {
                log.error("归档消息失败: messageId={}", deadMessage.getMessageId(), e);
            }
        }

        log.info("自动归档完成，共归档 {} 条消息", archivedCount);
    }

    @Scheduled(cron = "0 0 3 * * ?")
    public void autoCleanArchivedMessages() {
        if (!archiveEnabled) {
            log.debug("自动清理归档已禁用");
            return;
        }

        log.info("开始自动清理归档消息...");

        int cleanDays = archiveThresholdDays * 4;
        LocalDateTime beforeTime = LocalDateTime.now().minusDays(cleanDays);

        int count = archivedMessageRepository.deleteByArchivedTimeBefore(beforeTime);

        log.info("自动清理归档消息完成，共清理 {} 条消息", count);
    }
}
