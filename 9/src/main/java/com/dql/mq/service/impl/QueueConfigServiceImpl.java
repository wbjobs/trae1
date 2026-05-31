package com.dql.mq.service.impl;

import com.dql.mq.config.DynamicListenerConfig;
import com.dql.mq.config.QueueBindingConfig;
import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.PageResult;
import com.dql.mq.dto.QueueConfigDTO;
import com.dql.mq.entity.QueueConfig;
import com.dql.mq.repository.QueueConfigRepository;
import com.dql.mq.service.QueueConfigService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.data.domain.Pageable;
import org.springframework.data.domain.Sort;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.util.List;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class QueueConfigServiceImpl implements QueueConfigService {

    private final QueueConfigRepository queueConfigRepository;
    private final QueueBindingConfig queueBindingConfig;
    private final DynamicListenerConfig dynamicListenerConfig;

    @Override
    public QueueConfigDTO getById(Long id) {
        QueueConfig config = queueConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + id));
        return convertToDTO(config);
    }

    @Override
    public QueueConfigDTO getByQueueName(String queueName) {
        QueueConfig config = queueConfigRepository.findByQueueName(queueName)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + queueName));
        return convertToDTO(config);
    }

    @Override
    public List<QueueConfigDTO> getAll() {
        return queueConfigRepository.findAll().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());
    }

    @Override
    public PageResult<QueueConfigDTO> getPage(int page, int size) {
        Pageable pageable = PageRequest.of(page, size, Sort.by(Sort.Direction.DESC, "createdTime"));
        Page<QueueConfig> pageResult = queueConfigRepository.findAll(pageable);

        List<QueueConfigDTO> dtoList = pageResult.getContent().stream()
                .map(this::convertToDTO)
                .collect(Collectors.toList());

        return PageResult.of(dtoList, page, size, pageResult.getTotalElements());
    }

    @Override
    @Transactional
    public ApiResponse<QueueConfigDTO> create(QueueConfigDTO dto) {
        if (queueConfigRepository.existsByQueueName(dto.getQueueName())) {
            return ApiResponse.error("队列名称已存在");
        }

        QueueConfig config = convertToEntity(dto);
        config.setCreatedTime(LocalDateTime.now());

        QueueConfig saved = queueConfigRepository.save(config);

        try {
            queueBindingConfig.declareQueueAndBindings(saved);
            if (saved.getEnabled() != null && saved.getEnabled()) {
                dynamicListenerConfig.createAndStartListener(saved);
            }
        } catch (Exception e) {
            log.error("创建队列绑定或监听器失败", e);
        }

        return ApiResponse.success("创建成功", convertToDTO(saved));
    }

    @Override
    @Transactional
    public ApiResponse<QueueConfigDTO> update(Long id, QueueConfigDTO dto) {
        QueueConfig existing = queueConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + id));

        String oldQueueName = existing.getQueueName();
        String oldExchangeName = existing.getExchangeName();
        String oldRoutingKey = existing.getRoutingKey();
        String oldDlqQueueName = existing.getDlqQueueName();
        boolean queueNameChanged = dto.getQueueName() != null && !oldQueueName.equals(dto.getQueueName());

        if (queueNameChanged) {
            if (queueConfigRepository.existsByQueueName(dto.getQueueName())) {
                return ApiResponse.error("队列名称已存在");
            }
        }

        if (dto.getQueueName() != null) {
            existing.setQueueName(dto.getQueueName());
        }
        if (dto.getExchangeName() != null) {
            existing.setExchangeName(dto.getExchangeName());
        }
        if (dto.getRoutingKey() != null) {
            existing.setRoutingKey(dto.getRoutingKey());
        }
        if (dto.getDlqExchangeName() != null) {
            existing.setDlqExchangeName(dto.getDlqExchangeName());
        }
        if (dto.getDlqRoutingKey() != null) {
            existing.setDlqRoutingKey(dto.getDlqRoutingKey());
        }
        if (dto.getDlqQueueName() != null) {
            existing.setDlqQueueName(dto.getDlqQueueName());
        }
        if (dto.getMaxRetryCount() != null) {
            existing.setMaxRetryCount(dto.getMaxRetryCount());
        }
        if (dto.getRetryInterval() != null) {
            existing.setRetryInterval(dto.getRetryInterval());
        }
        if (dto.getEnabled() != null) {
            existing.setEnabled(dto.getEnabled());
        }
        if (dto.getAutoDlq() != null) {
            existing.setAutoDlq(dto.getAutoDlq());
        }
        if (dto.getAcknowledgeMode() != null) {
            existing.setAcknowledgeMode(dto.getAcknowledgeMode());
        }
        if (dto.getPrefetchCount() != null) {
            existing.setPrefetchCount(dto.getPrefetchCount());
        }
        if (dto.getConcurrency() != null) {
            existing.setConcurrency(dto.getConcurrency());
        }
        if (dto.getMaxConcurrency() != null) {
            existing.setMaxConcurrency(dto.getMaxConcurrency());
        }
        if (dto.getDescription() != null) {
            existing.setDescription(dto.getDescription());
        }
        existing.setUpdatedTime(LocalDateTime.now());

        QueueConfig saved = queueConfigRepository.save(existing);

        try {
            if (queueNameChanged) {
                dynamicListenerConfig.stopAndRemoveListener(oldQueueName);

                QueueConfig tempConfig = QueueConfig.builder()
                        .queueName(oldQueueName)
                        .exchangeName(oldExchangeName)
                        .routingKey(oldRoutingKey)
                        .dlqQueueName(oldDlqQueueName)
                        .build();
                try {
                    queueBindingConfig.removeQueueAndBindings(tempConfig);
                } catch (Exception e) {
                    log.warn("删除旧队列绑定失败，可能队列不存在: {}", oldQueueName, e);
                }
            }

            queueBindingConfig.declareQueueAndBindings(saved);

            if (saved.getEnabled() != null && saved.getEnabled()) {
                if (queueNameChanged) {
                    dynamicListenerConfig.createAndStartListener(saved);
                } else {
                    dynamicListenerConfig.restartListener(saved);
                }
            }
        } catch (Exception e) {
            log.error("更新队列绑定或监听器失败", e);
        }

        return ApiResponse.success("更新成功", convertToDTO(saved));
    }

    @Override
    @Transactional
    public ApiResponse<Void> delete(Long id) {
        QueueConfig config = queueConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + id));

        try {
            dynamicListenerConfig.stopAndRemoveListener(config.getQueueName());
            queueBindingConfig.removeQueueAndBindings(config);
        } catch (Exception e) {
            log.error("删除队列绑定或监听器失败", e);
        }

        queueConfigRepository.deleteById(id);
        return ApiResponse.success("删除成功", null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> updateEnabled(Long id, Boolean enabled) {
        QueueConfig config = queueConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + id));

        config.setEnabled(enabled);
        config.setUpdatedTime(LocalDateTime.now());
        queueConfigRepository.save(config);

        try {
            if (enabled) {
                dynamicListenerConfig.createAndStartListener(config);
            } else {
                dynamicListenerConfig.stopAndRemoveListener(config.getQueueName());
            }
        } catch (Exception e) {
            log.error("更新监听器状态失败", e);
        }

        return ApiResponse.success(enabled ? "已启用" : "已禁用", null);
    }

    @Override
    @Transactional
    public ApiResponse<Void> updateMaxRetryCount(Long id, Integer maxRetryCount) {
        QueueConfig config = queueConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("队列配置不存在: " + id));

        config.setMaxRetryCount(maxRetryCount);
        config.setUpdatedTime(LocalDateTime.now());
        queueConfigRepository.save(config);

        return ApiResponse.success("更新成功", null);
    }

    private QueueConfigDTO convertToDTO(QueueConfig config) {
        return QueueConfigDTO.builder()
                .id(config.getId())
                .queueName(config.getQueueName())
                .exchangeName(config.getExchangeName())
                .routingKey(config.getRoutingKey())
                .dlqExchangeName(config.getDlqExchangeName())
                .dlqRoutingKey(config.getDlqRoutingKey())
                .dlqQueueName(config.getDlqQueueName())
                .maxRetryCount(config.getMaxRetryCount())
                .retryInterval(config.getRetryInterval())
                .enabled(config.getEnabled())
                .autoDlq(config.getAutoDlq())
                .acknowledgeMode(config.getAcknowledgeMode())
                .prefetchCount(config.getPrefetchCount())
                .concurrency(config.getConcurrency())
                .maxConcurrency(config.getMaxConcurrency())
                .description(config.getDescription())
                .build();
    }

    private QueueConfig convertToEntity(QueueConfigDTO dto) {
        return QueueConfig.builder()
                .id(dto.getId())
                .queueName(dto.getQueueName())
                .exchangeName(dto.getExchangeName())
                .routingKey(dto.getRoutingKey())
                .dlqExchangeName(dto.getDlqExchangeName())
                .dlqRoutingKey(dto.getDlqRoutingKey())
                .dlqQueueName(dto.getDlqQueueName())
                .maxRetryCount(dto.getMaxRetryCount())
                .retryInterval(dto.getRetryInterval())
                .enabled(dto.getEnabled())
                .autoDlq(dto.getAutoDlq())
                .acknowledgeMode(dto.getAcknowledgeMode())
                .prefetchCount(dto.getPrefetchCount())
                .concurrency(dto.getConcurrency())
                .maxConcurrency(dto.getMaxConcurrency())
                .description(dto.getDescription())
                .build();
    }
}
