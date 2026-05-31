package com.dql.mq.service;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.ArchivedMessageDTO;
import com.dql.mq.dto.PageResult;

import java.util.List;

public interface ArchivedMessageService {

    ArchivedMessageDTO getById(Long id);

    PageResult<ArchivedMessageDTO> getByQueueName(String queueName, int page, int size);

    PageResult<ArchivedMessageDTO> getByQueueNameAndStatus(String queueName, String status, int page, int size);

    PageResult<ArchivedMessageDTO> getPage(int page, int size);

    ApiResponse<Void> deleteByIds(List<Long> ids);

    ApiResponse<Void> cleanArchivedBeforeDays(Integer days);

    ApiResponse<Integer> archiveDeadMessages(String queueName, String status, String reason);
}
