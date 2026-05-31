package com.dql.mq.service;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.MessageSendDTO;

public interface MessageSendService {

    ApiResponse<String> sendMessage(MessageSendDTO dto);

    ApiResponse<String> sendDelayMessage(MessageSendDTO dto, Long delayMs);
}
