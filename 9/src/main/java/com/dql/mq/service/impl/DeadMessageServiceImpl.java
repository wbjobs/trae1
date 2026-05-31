package com.dql.mq.service.impl;

import com.dql.mq.dto.*;
import com.dql.mq.entity.DeadMessage;
import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.ArchivedMessageRepository;
import com.dql.mq.repository.DeadMessageRepository;
import com.dql.mq.repository.QueueConfigRepository;
import com.dql.mq.service.DeadMessageService;
import com.dql.mq.util.MessageCategoryUtil;
import jakarta.persistence.criteria.Predicate;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.amqp.rabbit.core.RabbitTemplate;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.data.jpa.domain.Specification;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.*;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class DeadMessageServiceImpl implements DeadMessageService {

    private final DeadMessageRepository deadMessageRepository;
    private final QueueConfigRepository queueConfigRepository;
    private final ArchivedMessageRepository archivedMessageRepository;
    private final RabbitTemplate rabbitTemplate;

    @Override
    public DeadMessageDTO getById(Long id) {
        DeadMessage message = deadMessageRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("死信消息不存在: " + id));
        return convertToDTO(message);
    }

    @Override
    public DeadMessageDTO getByMessageId(String messageId) {
        DeadMessage message = deadMessageRepository.findByMessageId(messageId)
                .orElseThrow(() -> new RuntimeException("死信消息不存在: " + messageId));
        return convertToDTO(message);
    }

    @Override
    public PageResult<DeadMessageDTO> query(DeadMessageQueryDTO queryDTO) {
        Pageable pageable = PageRequest.of(queryDTO.getPage(), queryDTO.getSize(), Sort.by(Sort.Direction.DESC, "createdTime"));

        Specification<DeadMessage> spec = (root, query, cb) -> {
            List<Predicate> predicates = new ArrayList<>();

            if (queryDTO.getQueueName() != null && !queryDTO.getQueueName().isEmpty()) {
                predicates.add(cb.equal(root.get("queueName"), queryDTO.getQueueName()));
            }
            if (queryDTO.getStatus() != null && !queryDTO.getStatus().isEmpty()) {
                predicates.add(cb.equal(root.get("status"), queryDTO.getStatus()));
            }
            if (queryDTO.getMessageId() != null && !queryDTO.getMessageId().isEmpty()) {
                predicates.add(cb.like(root.get("messageId"), "%" + queryDTO.getMessageId() + "%"));
            }
            if (queryDTO.getStartTime() != null) {
                predicates.add(cb.greaterThanOrEqualTo(root.get("createdTime"), queryDTO.getStartTime()));
            }
            if (queryDTO.getEndTime() != null) {
                predicates.add(cb.lessThanOrEqualTo(root.get("createdTime"), queryDTO.getEndTime()));
            }

            return cb.and(predicates.toArray(new Predicate[0]));
        };

        Page<DeadMessage> page = deadMessageRepository.findAll(spec, pageable);

        List<DeadMessageDTO> dtoList = page.getContent().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());

        return PageResult.of(dtoList, queryDTO.getPage(), queryDTO.getSize(), page.getTotalElements());
    }

    @Override
    @Transactional
    public ApiResponse<Void> retry(RetryRequestDTO requestDTO) {
        DeadMessage message = deadMessageRepository.findById(requestDTO.getMessageId())
                .orElseThrow(() -> new RuntimeException("死信消息不存在: " + requestDTO.getMessageId()));

        if (!requestDTO.getForceRetry() && message.getRetryCount() >= message.getMaxRetryCount()) {
            return ApiResponse.error("消息已达到最大重试次数，需强制重试");
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

        if (exchangeName == null || exchangeName.isEmpty()) {
            return ApiResponse.error("交换机名称为空，无法重试消息");
        }
        if (routingKey == null || routingKey.isEmpty()) {
            return ApiResponse.error("路由键为空，无法重试消息");
        }

        try {
            Map<String, Object> headers = new HashMap<>();
            headers.put("x-retry-count", message.getRetryCount() + 1);
            headers.put("x-original-message-id", message.getMessageId());

            final String finalExchangeName = exchangeName;
            final String finalRoutingKey = routingKey;
            final String originalMessageId = message.getMessageId();

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
            if (requestDTO.getRemark() != null && !requestDTO.getRemark().isEmpty()) {
                message.setRemark(requestDTO.getRemark());
            }
            deadMessageRepository.save(message);

            log.info("消息重试成功: messageId={}, exchange={}, routingKey={}, retryCount={}",
                    message.getMessageId(), finalExchangeName, finalRoutingKey, message.getRetryCount());
            return ApiResponse.success("重试成功", null);

        } catch (Exception e) {
            log.error("消息重试失败: messageId={}", message.getMessageId(), e);
            return ApiResponse.error("重试失败: " + e.getMessage());
        }
    }

    @Override
    @Transactional
    public ApiResponse<Void> batchRetry(BatchRetryRequestDTO requestDTO) {
        if (requestDTO.getMessageIds() == null || requestDTO.getMessageIds().isEmpty()) {
            return ApiResponse.error("消息ID列表不能为空");
        }

        int successCount = 0;
        int failCount = 0;

        for (Long id : requestDTO.getMessageIds()) {
            try {
                RetryRequestDTO retryDTO = RetryRequestDTO.builder()
                        .messageId(id)
                        .forceRetry(requestDTO.getForceRetry())
                        .remark(requestDTO.getRemark())
                        .build();
                ApiResponse<Void> result = retry(retryDTO);
                if (result.getCode() == 200) {
                    successCount++;
                } else {
                    failCount++;
                }
            } catch (Exception e) {
                failCount++;
                log.error("批量重试消息失败: id={}", id, e);
            }
        }

        String message = String.format("批量重试完成，成功: %d, 失败: %d", successCount, failCount);
        return ApiResponse.success(message, null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> clean(BatchCleanRequestDTO requestDTO) {
        if (requestDTO.getMessageIds() == null || requestDTO.getMessageIds().isEmpty()) {
            return ApiResponse.error("消息ID列表不能为空");
        }

        if ("ARCHIVE".equalsIgnoreCase(requestDTO.getCleanType())) {
            int archivedCount = 0;
            for (Long id : requestDTO.getMessageIds()) {
                DeadMessage message = deadMessageRepository.findById(id).orElse(null);
                if (message != null) {
                    com.dql.mq.entity.ArchivedMessage archived = com.dql.mq.entity.ArchivedMessage.builder()
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
                            .archiveReason(requestDTO.getArchiveReason() != null
                                    ? requestDTO.getArchiveReason() : "手动归档清理")
                            .build();

                    archivedMessageRepository.save(archived);
                    deadMessageRepository.deleteById(id);
                    archivedCount++;
                }
            }
            return ApiResponse.success("归档清理成功，共归档 " + archivedCount + " 条消息", null);
        } else {
            deadMessageRepository.deleteByIds(requestDTO.getMessageIds());
            return ApiResponse.success("清理成功，共清理 " + requestDTO.getMessageIds().size() + " 条消息", null);
        }
    }

    @Override
    @Transactional
    public ApiResponse<Void> cleanByStatus(String queueName, String status) {
        if (queueName == null || queueName.isEmpty()) {
            return ApiResponse.error("队列名称不能为空");
        }
        if (status == null || status.isEmpty()) {
            return ApiResponse.error("状态不能为空");
        }

        int count = deadMessageRepository.deleteByQueueNameAndStatus(queueName, status);
        return ApiResponse.success("清理成功，共清理 " + count + " 条消息", null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> updateStatus(Long id, String status) {
        DeadMessage message = deadMessageRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("死信消息不存在: " + id));

        message.setStatus(status);
        message.setUpdatedTime(LocalDateTime.now());
        deadMessageRepository.save(message);

        return ApiResponse.success("状态更新成功", null);
    }

    @Override
    public List<QueueStatsDTO> getQueueStats() {
        List<Object[]> results = deadMessageRepository.countGroupByQueueNameAndStatus();
        Map<String, QueueStatsDTO> statsMap = new LinkedHashMap<>();

        for (Object[] result : results) {
            String queueName = (String) result[0];
            String status = (String) result[1];
            Long count = (Long) result[2];

            QueueStatsDTO stats = statsMap.computeIfAbsent(queueName, k -> QueueStatsDTO.builder()
                    .queueName(k)
                    .pendingCount(0L)
                    .retryingCount(0L)
                    .successCount(0L)
                    .failedCount(0L)
                    .deadLetterCount(0L)
                    .archivedCount(0L)
                    .totalCount(0L)
                    .build());

            switch (status) {
                case "PENDING":
                    stats.setPendingCount(count);
                    break;
                case "RETRYING":
                    stats.setRetryingCount(count);
                    break;
                case "SUCCESS":
                    stats.setSuccessCount(count);
                    break;
                case "FAILED":
                    stats.setFailedCount(count);
                    break;
                case "DEAD_LETTER":
                    stats.setDeadLetterCount(count);
                    break;
            }
            stats.setTotalCount(stats.getTotalCount() + count);
        }

        for (QueueStatsDTO stats : statsMap.values()) {
            Long archivedCount = archivedMessageRepository.countByQueueName(stats.getQueueName());
            stats.setArchivedCount(archivedCount != null ? archivedCount : 0L);
        }

        return new ArrayList<>(statsMap.values());
    }

    @Override
    public List<CategoryStatsDTO> getCategoryStats() {
        List<Object[]> categoryResults = deadMessageRepository.countGroupByCategory();
        List<Object[]> categoryStatusResults = deadMessageRepository.countGroupByCategoryAndStatus();

        Map<String, CategoryStatsDTO> statsMap = new LinkedHashMap<>();

        for (Object[] result : categoryResults) {
            String category = (String) result[0];
            Long count = (Long) result[1];

            CategoryStatsDTO stats = CategoryStatsDTO.builder()
                    .category(category)
                    .categoryName(getCategoryDisplayName(category))
                    .count(count)
                    .successCount(0L)
                    .failedCount(0L)
                    .retryingCount(0L)
                    .deadLetterCount(0L)
                    .build();

            statsMap.put(category, stats);
        }

        for (Object[] result : categoryStatusResults) {
            String category = (String) result[0];
            String status = (String) result[1];
            Long count = (Long) result[2];

            CategoryStatsDTO stats = statsMap.get(category);
            if (stats != null) {
                switch (status) {
                    case "SUCCESS":
                        stats.setSuccessCount(count);
                        break;
                    case "FAILED":
                        stats.setFailedCount(count);
                        break;
                    case "RETRYING":
                        stats.setRetryingCount(count);
                        break;
                    case "DEAD_LETTER":
                        stats.setDeadLetterCount(count);
                        break;
                }
            }
        }

        return new ArrayList<>(statsMap.values());
    }

    @Override
    public List<ErrorTypeStatsDTO> getErrorTypeStats() {
        List<Object[]> results = deadMessageRepository.countGroupByErrorType();

        List<ErrorTypeStatsDTO> statsList = new ArrayList<>();
        for (Object[] result : results) {
            String errorType = (String) result[0];
            Long count = (Long) result[1];

            ErrorTypeStatsDTO stats = ErrorTypeStatsDTO.builder()
                    .errorType(errorType)
                    .errorTypeName(getErrorTypeDisplayName(errorType))
                    .count(count)
                    .queueCount(0L)
                    .successCount(0L)
                    .failedCount(0L)
                    .build();

            statsList.add(stats);
        }

        return statsList;
    }

    @Override
    public ErrorAnalysisDTO getErrorAnalysis() {
        LocalDateTime now = LocalDateTime.now();

        Long totalCount = deadMessageRepository.count();
        Long successCount = deadMessageRepository.countGroupByQueueNameAndStatus().stream()
                .filter(r -> "SUCCESS".equals(r[1]))
                .mapToLong(r -> (Long) r[2])
                .sum();
        Long failedCount = deadMessageRepository.countGroupByQueueNameAndStatus().stream()
                .filter(r -> "FAILED".equals(r[1]))
                .mapToLong(r -> (Long) r[2])
                .sum();
        Long retryingCount = deadMessageRepository.countGroupByQueueNameAndStatus().stream()
                .filter(r -> "RETRYING".equals(r[1]))
                .mapToLong(r -> (Long) r[2])
                .sum();
        Long deadLetterCount = deadMessageRepository.countGroupByQueueNameAndStatus().stream()
                .filter(r -> "DEAD_LETTER".equals(r[1]))
                .mapToLong(r -> (Long) r[2])
                .sum();

        Long totalRetryCount = 0L;
        try {
            Page<DeadMessage> allMessages = deadMessageRepository.findAll(PageRequest.of(0, 1));
            if (allMessages.hasContent()) {
                totalRetryCount = deadMessageRepository.findAll().stream()
                        .mapToLong(m -> m.getRetryCount() != null ? m.getRetryCount() : 0)
                        .sum();
            }
        } catch (Exception e) {
            log.warn("计算总重试次数失败", e);
        }

        Double averageRetryCount = totalCount > 0 ? (double) totalRetryCount / totalCount : 0.0;

        List<CategoryStatsDTO> categoryStats = getCategoryStats();
        List<ErrorTypeStatsDTO> errorTypeStats = getErrorTypeStats();

        List<ErrorCodeStatsDTO> errorCodeStats = deadMessageRepository.countGroupByErrorCode().stream()
                .limit(20)
                .map(r -> ErrorCodeStatsDTO.builder()
                        .errorCode((String) r[0])
                        .errorType(determineErrorTypeFromCode((String) r[0]))
                        .count((Long) r[1])
                        .build())
                .collect(Collectors.toList());

        List<ErrorMessageStatsDTO> topErrorMessages = deadMessageRepository.countGroupByErrorMessage().stream()
                .sorted((a, b) -> Long.compare((Long) b[1], (Long) a[1]))
                .limit(10)
                .map(r -> ErrorMessageStatsDTO.builder()
                        .errorMessage(truncateMessage((String) r[0], 100))
                        .errorType("SYSTEM")
                        .count((Long) r[1])
                        .build())
                .collect(Collectors.toList());

        List<QueueErrorStatsDTO> queueErrorStats = getQueueErrorStats();

        Map<String, Long> retryableErrorCount = new HashMap<>();
        Map<String, Long> nonRetryableErrorCount = new HashMap<>();

        for (ErrorTypeStatsDTO stats : errorTypeStats) {
            boolean retriable = MessageCategoryUtil.isRetriableError(stats.getErrorType(), null);
            if (retriable) {
                retryableErrorCount.put(stats.getErrorType(), stats.getCount());
            } else {
                nonRetryableErrorCount.put(stats.getErrorType(), stats.getCount());
            }
        }

        return ErrorAnalysisDTO.builder()
                .totalErrorCount(totalCount)
                .totalDeadLetterCount(deadLetterCount)
                .totalFailedCount(failedCount)
                .totalRetryingCount(retryingCount)
                .totalSuccessCount(successCount)
                .categoryStats(categoryStats)
                .errorTypeStats(errorTypeStats)
                .errorCodeStats(errorCodeStats)
                .topErrorMessages(topErrorMessages)
                .queueErrorStats(queueErrorStats)
                .retryableErrorCount(retryableErrorCount)
                .nonRetryableErrorCount(nonRetryableErrorCount)
                .averageRetryCount(averageRetryCount)
                .totalRetryCount(totalRetryCount)
                .analysisTime(now.format(DateTimeFormatter.ISO_LOCAL_DATE_TIME))
                .build();
    }

    @Override
    public List<QueueErrorStatsDTO> getQueueErrorStats() {
        List<Object[]> queueStatusResults = deadMessageRepository.countGroupByQueueNameAndStatus();
        List<Object[]> queueCategoryResults = deadMessageRepository.countGroupByQueueNameAndCategory();

        Map<String, QueueErrorStatsDTO> statsMap = new LinkedHashMap<>();
        Map<String, Map<String, Long>> queueCategoryCounts = new HashMap<>();

        for (Object[] result : queueStatusResults) {
            String queueName = (String) result[0];
            String status = (String) result[1];
            Long count = (Long) result[2];

            QueueErrorStatsDTO stats = statsMap.computeIfAbsent(queueName, k -> QueueErrorStatsDTO.builder()
                    .queueName(k)
                    .totalErrorCount(0L)
                    .failedCount(0L)
                    .deadLetterCount(0L)
                    .retryingCount(0L)
                    .successCount(0L)
                    .errorRate(0.0)
                    .build());

            stats.setTotalErrorCount(stats.getTotalErrorCount() + count);
            switch (status) {
                case "FAILED":
                    stats.setFailedCount(count);
                    break;
                case "DEAD_LETTER":
                    stats.setDeadLetterCount(count);
                    break;
                case "RETRYING":
                    stats.setRetryingCount(count);
                    break;
                case "SUCCESS":
                    stats.setSuccessCount(count);
                    break;
            }
        }

        for (Object[] result : queueCategoryResults) {
            String queueName = (String) result[0];
            String category = (String) result[1];
            Long count = (Long) result[2];

            queueCategoryCounts.computeIfAbsent(queueName, k -> new HashMap<>())
                    .put(category, count);
        }

        for (QueueErrorStatsDTO stats : statsMap.values()) {
            if (stats.getTotalErrorCount() > 0) {
                stats.setErrorRate((double) (stats.getFailedCount() + stats.getDeadLetterCount()) / stats.getTotalErrorCount());
            }

            Map<String, Long> categoryCounts = queueCategoryCounts.get(stats.getQueueName());
            if (categoryCounts != null && !categoryCounts.isEmpty()) {
                stats.setTopCategory(categoryCounts.entrySet().stream()
                        .max(Map.Entry.comparingByValue())
                        .map(Map.Entry::getKey)
                        .orElse(null));
            }
        }

        return new ArrayList<>(statsMap.values());
    }

    @Override
    @Transactional
    public ApiResponse<Void> updateCategory(Long id, String category) {
        DeadMessage message = deadMessageRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("死信消息不存在: " + id));

        message.setCategory(category);
        message.setUpdatedTime(LocalDateTime.now());
        deadMessageRepository.save(message);

        return ApiResponse.success("分类更新成功", null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> batchUpdateCategory(List<Long> ids, String category) {
        if (ids == null || ids.isEmpty()) {
            return ApiResponse.error("消息ID列表不能为空");
        }

        int updatedCount = 0;
        for (Long id : ids) {
            try {
                Optional<DeadMessage> messageOpt = deadMessageRepository.findById(id);
                if (messageOpt.isPresent()) {
                    DeadMessage message = messageOpt.get();
                    message.setCategory(category);
                    message.setUpdatedTime(LocalDateTime.now());
                    deadMessageRepository.save(message);
                    updatedCount++;
                }
            } catch (Exception e) {
                log.error("更新消息分类失败: id={}", id, e);
            }
        }

        return ApiResponse.success("批量分类更新成功，共更新 " + updatedCount + " 条消息", null);
    }

    private String getCategoryDisplayName(String category) {
        if (category == null) {
            return "未知";
        }
        switch (category) {
            case MessageCategoryUtil.CATEGORY_BUSINESS:
                return "业务异常";
            case MessageCategoryUtil.CATEGORY_SYSTEM:
                return "系统异常";
            case MessageCategoryUtil.CATEGORY_NETWORK:
                return "网络异常";
            case MessageCategoryUtil.CATEGORY_DATA:
                return "数据异常";
            case MessageCategoryUtil.CATEGORY_TIMEOUT:
                return "超时异常";
            case MessageCategoryUtil.CATEGORY_VALIDATION:
                return "校验异常";
            case MessageCategoryUtil.CATEGORY_DATABASE:
                return "数据库异常";
            case MessageCategoryUtil.CATEGORY_EXTERNAL:
                return "外部服务异常";
            default:
                return "未知分类";
        }
    }

    private String getErrorTypeDisplayName(String errorType) {
        if (errorType == null) {
            return "未知";
        }
        switch (errorType) {
            case MessageCategoryUtil.ERROR_TYPE_NULL_POINTER:
                return "空指针异常";
            case MessageCategoryUtil.ERROR_TYPE_IO:
                return "IO异常";
            case MessageCategoryUtil.ERROR_TYPE_SQL:
                return "SQL异常";
            case MessageCategoryUtil.ERROR_TYPE_TIMEOUT:
                return "超时异常";
            case MessageCategoryUtil.ERROR_TYPE_CONNECTION:
                return "连接异常";
            case MessageCategoryUtil.ERROR_TYPE_PARSE:
                return "解析异常";
            case MessageCategoryUtil.ERROR_TYPE_VALIDATION:
                return "校验异常";
            case MessageCategoryUtil.ERROR_TYPE_AUTHENTICATION:
                return "认证异常";
            case MessageCategoryUtil.ERROR_TYPE_PERMISSION:
                return "权限异常";
            case MessageCategoryUtil.ERROR_TYPE_BUSINESS:
                return "业务异常";
            default:
                return "系统异常";
        }
    }

    private String determineErrorTypeFromCode(String errorCode) {
        if (errorCode == null) {
            return "SYSTEM";
        }
        if (errorCode.startsWith("NPE")) {
            return MessageCategoryUtil.ERROR_TYPE_NULL_POINTER;
        }
        if (errorCode.startsWith("TIMEOUT")) {
            return MessageCategoryUtil.ERROR_TYPE_TIMEOUT;
        }
        if (errorCode.startsWith("SQL")) {
            return MessageCategoryUtil.ERROR_TYPE_SQL;
        }
        if (errorCode.startsWith("CONN")) {
            return MessageCategoryUtil.ERROR_TYPE_CONNECTION;
        }
        if (errorCode.startsWith("PARSE")) {
            return MessageCategoryUtil.ERROR_TYPE_PARSE;
        }
        if (errorCode.startsWith("VALID")) {
            return MessageCategoryUtil.ERROR_TYPE_VALIDATION;
        }
        if (errorCode.startsWith("AUTH")) {
            return MessageCategoryUtil.ERROR_TYPE_AUTHENTICATION;
        }
        if (errorCode.startsWith("PERM")) {
            return MessageCategoryUtil.ERROR_TYPE_PERMISSION;
        }
        if (errorCode.startsWith("IO")) {
            return MessageCategoryUtil.ERROR_TYPE_IO;
        }
        if (errorCode.startsWith("BIZ")) {
            return MessageCategoryUtil.ERROR_TYPE_BUSINESS;
        }
        return MessageCategoryUtil.ERROR_TYPE_SYSTEM;
    }

    private String truncateMessage(String message, int maxLength) {
        if (message == null) {
            return null;
        }
        if (message.length() <= maxLength) {
            return message;
        }
        return message.substring(0, maxLength) + "...";
    }

    private DeadMessageDTO convertToDTO(DeadMessage message) {
        return DeadMessageDTO.builder()
                .id(message.getId())
                .messageId(message.getMessageId())
                .queueName(message.getQueueName())
                .exchangeName(message.getExchangeName())
                .routingKey(message.getRoutingKey())
                .messageBody(message.getMessageBody())
                .headers(message.getHeaders())
                .errorMessage(message.getErrorMessage())
                .retryCount(message.getRetryCount())
                .maxRetryCount(message.getMaxRetryCount())
                .status(message.getStatus())
                .createdTime(message.getCreatedTime())
                .updatedTime(message.getUpdatedTime())
                .nextRetryTime(message.getNextRetryTime())
                .originalCreatedTime(message.getOriginalCreatedTime())
                .consumer(message.getConsumer())
                .remark(message.getRemark())
                .category(message.getCategory())
                .errorType(message.getErrorType())
                .errorCode(message.getErrorCode())
                .build();
    }
}
