package com.dql.mq.service;

import com.dql.mq.dto.*;
import org.springframework.data.domain.Pageable;

import java.util.List;

public interface DeadMessageService {

    DeadMessageDTO getById(Long id);

    DeadMessageDTO getByMessageId(String messageId);

    PageResult<DeadMessageDTO> query(DeadMessageQueryDTO queryDTO);

    ApiResponse<Void> retry(RetryRequestDTO requestDTO);

    ApiResponse<Void> batchRetry(BatchRetryRequestDTO requestDTO);

    ApiResponse<Void> clean(BatchCleanRequestDTO requestDTO);

    ApiResponse<Void> cleanByStatus(String queueName, String status);

    ApiResponse<Void> updateStatus(Long id, String status);

    List<QueueStatsDTO> getQueueStats();

    List<CategoryStatsDTO> getCategoryStats();

    List<ErrorTypeStatsDTO> getErrorTypeStats();

    ErrorAnalysisDTO getErrorAnalysis();

    List<QueueErrorStatsDTO> getQueueErrorStats();

    ApiResponse<Void> updateCategory(Long id, String category);

    ApiResponse<Void> batchUpdateCategory(List<Long> ids, String category);
}
