package com.dql.mq.service.impl;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.ArchivedMessageDTO;
import com.dql.mq.dto.PageResult;
import com.dql.mq.entity.ArchivedMessage;
import com.dql.mq.entity.DeadMessage;
import com.dql.mq.repository.ArchivedMessageRepository;
import com.dql.mq.repository.DeadMessageRepository;
import com.dql.mq.service.ArchivedMessageService;
import jakarta.persistence.criteria.Predicate;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.data.jpa.domain.Specification;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class ArchivedMessageServiceImpl implements ArchivedMessageService {

    private final ArchivedMessageRepository archivedMessageRepository;
    private final DeadMessageRepository deadMessageRepository;

    @Override
    public ArchivedMessageDTO getById(Long id) {
        ArchivedMessage message = archivedMessageRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("归档消息不存在: " + id));
        return convertToDTO(message);
    }

    @Override
    public PageResult<ArchivedMessageDTO> getByQueueName(String queueName, int page, int size) {
        Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "archivedTime"));
        Page<ArchivedMessage> pageResult = archivedMessageRepository.findByQueueName(queueName, pageable);

        List<ArchivedMessageDTO> dtoList = pageResult.getContent().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());

        return PageResult.of(dtoList, page, size, pageResult.getTotalElements());
    }

    @Override
    public PageResult<ArchivedMessageDTO> getByQueueNameAndStatus(String queueName, String status, int page, int size) {
        Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "archivedTime"));
        Page<ArchivedMessage> pageResult = archivedMessageRepository.findByQueueNameAndFinalStatus(queueName, status, pageable);

        List<ArchivedMessageDTO> dtoList = pageResult.getContent().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());

        return PageResult.of(dtoList, page, size, pageResult.getTotalElements());
    }

    @Override
    public PageResult<ArchivedMessageDTO> getPage(int page, int size) {
        Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "archivedTime"));
        Page<ArchivedMessage> pageResult = archivedMessageRepository.findAll(pageable);

        List<ArchivedMessageDTO> dtoList = pageResult.getContent().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());

        return PageResult.of(dtoList, page, size, pageResult.getTotalElements());
    }

    @Override
    @Transactional
    public ApiResponse<Void> deleteByIds(List<Long> ids) {
        if (ids == null || ids.isEmpty()) {
            return ApiResponse.error("ID列表不能为空");
        }

        int count = archivedMessageRepository.deleteByIds(ids);
        return ApiResponse.success("删除成功，共删除 " + count + " 条归档消息", null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> cleanArchivedBeforeDays(Integer days) {
        if (days == null || days <= 0) {
            return ApiResponse.error("天数必须大于0");
        }

        LocalDateTime beforeTime = LocalDateTime.now().minusDays(days);
        int count = archivedMessageRepository.deleteByArchivedTimeBefore(beforeTime);

        return ApiResponse.success("清理成功，共清理 " + count + " 条归档消息", null);
    }

    @Override
    @Transactional
    public ApiResponse<Integer> archiveDeadMessages(String queueName, String status, String reason) {
        Specification<DeadMessage> spec = (root, query, cb) -> {
            List<Predicate> predicates = new ArrayList<>();

            if (queueName != null && !queueName.isEmpty()) {
                predicates.add(cb.equal(root.get("queueName"), queueName));
            }
            if (status != null && !status.isEmpty()) {
                predicates.add(cb.equal(root.get("status"), status));
            }

            return cb.and(predicates.toArray(new Predicate[0]));
        };

        List<DeadMessage> deadMessages = deadMessageRepository.findAll(spec);

        if (deadMessages.isEmpty()) {
            return ApiResponse.success("没有需要归档的消息", 0);
        }

        int archivedCount = 0;
        for (DeadMessage deadMessage : deadMessages) {
            try {
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
                        .archiveReason(reason != null ? reason : "手动归档")
                        .build();

                archivedMessageRepository.save(archived);
                deadMessageRepository.deleteById(deadMessage.getId());
                archivedCount++;

            } catch (Exception e) {
                log.error("归档消息失败: messageId={}", deadMessage.getMessageId(), e);
            }
        }

        return ApiResponse.success("归档成功，共归档 " + archivedCount + " 条消息", archivedCount);
    }

    private ArchivedMessageDTO convertToDTO(ArchivedMessage message) {
        return ArchivedMessageDTO.builder()
                .id(message.getId())
                .messageId(message.getMessageId())
                .queueName(message.getQueueName())
                .exchangeName(message.getExchangeName())
                .routingKey(message.getRoutingKey())
                .messageBody(message.getMessageBody())
                .errorMessage(message.getErrorMessage())
                .retryCount(message.getRetryCount())
                .finalStatus(message.getFinalStatus())
                .archivedTime(message.getArchivedTime())
                .originalCreatedTime(message.getOriginalCreatedTime())
                .archiveReason(message.getArchiveReason())
                .build();
    }
}
