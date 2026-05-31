package com.dql.mq.controller;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.MessageSendDTO;
import com.dql.mq.service.MessageSendService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

@Slf4j
@RestController
@RequestMapping("/messages")
@RequiredArgsConstructor
@Tag(name = "消息发送管理", description = "消息发送接口")
public class MessageSendController {

    private final MessageSendService messageSendService;

    @PostMapping("/send")
    @Operation(summary = "发送消息", description = "发送普通消息到指定队列")
    public ApiResponse<String> sendMessage(@Valid @RequestBody MessageSendDTO dto) {
        try {
            return messageSendService.sendMessage(dto);
        } catch (Exception e) {
            log.error("发送消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/send-delay")
    @Operation(summary = "发送延迟消息", description = "发送延迟消息到指定队列")
    public ApiResponse<String> sendDelayMessage(
            @Valid @RequestBody MessageSendDTO dto,
            @RequestParam Long delayMs) {
        try {
            return messageSendService.sendDelayMessage(dto, delayMs);
        } catch (Exception e) {
            log.error("发送延迟消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }
}
