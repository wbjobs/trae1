package com.dql.mq.controller;

import com.dql.mq.config.DynamicListenerConfig;
import com.dql.mq.dto.*;
import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.ArchivedMessageRepository;
import com.dql.mq.repository.DeadMessageRepository;
import com.dql.mq.repository.QueueConfigRepository;
import com.dql.mq.service.DeadMessageService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.time.LocalDateTime;
import java.util.*;
import java.util.stream.Collectors;

@Slf4j
@RestController
@RequestMapping("/ops")
@RequiredArgsConstructor
@Tag(name = "运维管控", description = "队列运维统计、监控等接口")
public class QueueOpsController {

    private final QueueConfigRepository queueConfigRepository;
    private final DeadMessageRepository deadMessageRepository;
    private final ArchivedMessageRepository archivedMessageRepository;
    private final DynamicListenerConfig dynamicListenerConfig;
    private final DeadMessageService deadMessageService;

    @GetMapping("/stats")
    @Operation(summary = "获取运维统计信息", description = "获取全面的队列运维统计信息，包括队列状态、消息统计、监听器状态等")
    public ApiResponse<QueueOpsStatsDTO> getOpsStats() {
        try {
            LocalDateTime now = LocalDateTime.now();

            List<QueueConfig> allConfigs = queueConfigRepository.findAll();
            long totalQueueCount = allConfigs.size();
            long activeQueueCount = allConfigs.stream().filter(q -> Boolean.TRUE.equals(q.getEnabled())).count();
            long disabledQueueCount = totalQueueCount - activeQueueCount;

            long totalDeadMessageCount = deadMessageRepository.count();
            long totalArchivedMessageCount = archivedMessageRepository.count();

            List<Object[]> statusCounts = deadMessageRepository.countGroupByQueueNameAndStatus();
            long totalSuccessCount = 0;
            long totalFailedCount = 0;
            long totalRetryingCount = 0;
            long totalDeadLetterCount = 0;

            for (Object[] result : statusCounts) {
                String status = (String) result[1];
                Long count = (Long) result[2];
                switch (status) {
                    case "SUCCESS":
                        totalSuccessCount += count;
                        break;
                    case "FAILED":
                        totalFailedCount += count;
                        break;
                    case "RETRYING":
                        totalRetryingCount += count;
                        break;
                    case "DEAD_LETTER":
                        totalDeadLetterCount += count;
                        break;
                }
            }

            double overallSuccessRate = totalDeadMessageCount > 0
                    ? (double) totalSuccessCount / totalDeadMessageCount * 100
                    : 0.0;
            double overallFailureRate = totalDeadMessageCount > 0
                    ? (double) (totalFailedCount + totalDeadLetterCount) / totalDeadMessageCount * 100
                    : 0.0;

            Map<String, Boolean> listenerStatus = dynamicListenerConfig.getAllListenerStatus();

            List<QueueDetailStatsDTO> queueDetails = buildQueueDetails(allConfigs, statusCounts, listenerStatus);

            List<CategoryStatsDTO> categoryStats = deadMessageService.getCategoryStats();
            List<ErrorTypeStatsDTO> errorTypeStats = deadMessageService.getErrorTypeStats();

            QueueOpsStatsDTO stats = QueueOpsStatsDTO.builder()
                    .statsTime(now)
                    .totalQueueCount(totalQueueCount)
                    .activeQueueCount(activeQueueCount)
                    .disabledQueueCount(disabledQueueCount)
                    .totalDeadMessageCount(totalDeadMessageCount)
                    .totalArchivedMessageCount(totalArchivedMessageCount)
                    .totalSuccessMessageCount(totalSuccessCount)
                    .totalFailedMessageCount(totalFailedCount)
                    .totalRetryingMessageCount(totalRetryingCount)
                    .totalDeadLetterCount(totalDeadLetterCount)
                    .overallSuccessRate(overallSuccessRate)
                    .overallFailureRate(overallFailureRate)
                    .listenerStatus(listenerStatus)
                    .queueDetails(queueDetails)
                    .categoryStats(categoryStats)
                    .errorTypeStats(errorTypeStats)
                    .build();

            return ApiResponse.success(stats);
        } catch (Exception e) {
            log.error("获取运维统计信息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/listener-status")
    @Operation(summary = "获取监听器状态", description = "获取所有消息监听器的运行状态")
    public ApiResponse<Map<String, Boolean>> getListenerStatus() {
        try {
            return ApiResponse.success(dynamicListenerConfig.getAllListenerStatus());
        } catch (Exception e) {
            log.error("获取监听器状态失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/queue-summary")
    @Operation(summary = "获取队列摘要", description = "获取所有队列的摘要信息")
    public ApiResponse<List<QueueDetailStatsDTO>> getQueueSummary() {
        try {
            List<QueueConfig> allConfigs = queueConfigRepository.findAll();
            List<Object[]> statusCounts = deadMessageRepository.countGroupByQueueNameAndStatus();
            Map<String, Boolean> listenerStatus = dynamicListenerConfig.getAllListenerStatus();

            List<QueueDetailStatsDTO> queueDetails = buildQueueDetails(allConfigs, statusCounts, listenerStatus);
            return ApiResponse.success(queueDetails);
        } catch (Exception e) {
            log.error("获取队列摘要失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    private List<QueueDetailStatsDTO> buildQueueDetails(List<QueueConfig> allConfigs,
                                                         List<Object[]> statusCounts,
                                                         Map<String, Boolean> listenerStatus) {
        Map<String, Map<String, Long>> queueStatusMap = new HashMap<>();
        for (Object[] result : statusCounts) {
            String queueName = (String) result[0];
            String status = (String) result[1];
            Long count = (Long) result[2];

            queueStatusMap.computeIfAbsent(queueName, k -> new HashMap<>())
                    .put(status, count);
        }

        List<Object[]> queueCategoryResults = deadMessageRepository.countGroupByQueueNameAndCategory();
        Map<String, Map<String, Long>> queueCategoryMap = new HashMap<>();
        for (Object[] result : queueCategoryResults) {
            String queueName = (String) result[0];
            String category = (String) result[1];
            Long count = (Long) result[2];

            queueCategoryMap.computeIfAbsent(queueName, k -> new HashMap<>())
                    .put(category, count);
        }

        return allConfigs.stream().map(config -> {
            String queueName = config.getQueueName();
            Map<String, Long> statusMap = queueStatusMap.getOrDefault(queueName, new HashMap<>());

            long successCount = statusMap.getOrDefault("SUCCESS", 0L);
            long failedCount = statusMap.getOrDefault("FAILED", 0L);
            long retryingCount = statusMap.getOrDefault("RETRYING", 0L);
            long deadLetterCount = statusMap.getOrDefault("DEAD_LETTER", 0L);
            long totalCount = successCount + failedCount + retryingCount + deadLetterCount;

            double successRate = totalCount > 0 ? (double) successCount / totalCount * 100 : 0.0;
            double failureRate = totalCount > 0 ? (double) (failedCount + deadLetterCount) / totalCount * 100 : 0.0;

            Long archivedCount = archivedMessageRepository.countByQueueName(queueName);

            String topCategory = null;
            Map<String, Long> categoryMap = queueCategoryMap.get(queueName);
            if (categoryMap != null && !categoryMap.isEmpty()) {
                topCategory = categoryMap.entrySet().stream()
                        .max(Map.Entry.comparingByValue())
                        .map(Map.Entry::getKey)
                        .orElse(null);
            }

            boolean isListenerRunning = listenerStatus.getOrDefault(queueName, false);

            return QueueDetailStatsDTO.builder()
                    .queueName(queueName)
                    .description(config.getDescription())
                    .enabled(config.getEnabled())
                    .maxRetryCount(config.getMaxRetryCount())
                    .retryInterval(config.getRetryInterval())
                    .concurrency(config.getConcurrency())
                    .maxConcurrency(config.getMaxConcurrency())
                    .totalMessageCount(totalCount)
                    .successCount(successCount)
                    .failedCount(failedCount)
                    .retryingCount(retryingCount)
                    .deadLetterCount(deadLetterCount)
                    .archivedCount(archivedCount != null ? archivedCount : 0L)
                    .successRate(successRate)
                    .failureRate(failureRate)
                    .listenerRunning(isListenerRunning)
                    .topCategory(topCategory)
                    .build();
        }).collect(Collectors.toList());
    }
}
