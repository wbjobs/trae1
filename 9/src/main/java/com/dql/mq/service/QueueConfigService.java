package com.dql.mq.service;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.PageResult;
import com.dql.mq.dto.QueueConfigDTO;

import java.util.List;

public interface QueueConfigService {

    QueueConfigDTO getById(Long id);

    QueueConfigDTO getByQueueName(String queueName);

    List<QueueConfigDTO> getAll();

    PageResult<QueueConfigDTO> getPage(int page, int size);

    ApiResponse<QueueConfigDTO> create(QueueConfigDTO dto);

    ApiResponse<QueueConfigDTO> update(Long id, QueueConfigDTO dto);

    ApiResponse<Void> delete(Long id);

    ApiResponse<Void> updateEnabled(Long id, Boolean enabled);

    ApiResponse<Void> updateMaxRetryCount(Long id, Integer maxRetryCount);
}
