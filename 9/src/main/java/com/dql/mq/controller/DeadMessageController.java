package com.dql.mq.controller;

import com.dql.mq.dto.*;
import com.dql.mq.service.DeadMessageService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@Slf4j
@RestController
@RequestMapping("/dead-messages")
@RequiredArgsConstructor
@Tag(name = "死信消息管理", description = "死信消息查询、重试、清理、统计等接口")
public class DeadMessageController {

    private final DeadMessageService deadMessageService;

    @GetMapping("/{id}")
    @Operation(summary = "根据ID查询死信消息", description = "根据消息ID查询死信消息详情")
    public ApiResponse<DeadMessageDTO> getById(
            @Parameter(description = "死信消息ID") @PathVariable Long id) {
        try {
            return ApiResponse.success(deadMessageService.getById(id));
        } catch (Exception e) {
            log.error("查询死信消息失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/message/{messageId}")
    @Operation(summary = "根据消息ID查询死信消息", description = "根据业务消息ID查询死信消息详情")
    public ApiResponse<DeadMessageDTO> getByMessageId(
            @Parameter(description = "业务消息ID") @PathVariable String messageId) {
        try {
            return ApiResponse.success(deadMessageService.getByMessageId(messageId));
        } catch (Exception e) {
            log.error("查询死信消息失败: messageId={}", messageId, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/query")
    @Operation(summary = "分页查询死信消息", description = "根据条件分页查询死信消息列表")
    public ApiResponse<PageResult<DeadMessageDTO>> query(@RequestBody DeadMessageQueryDTO queryDTO) {
        try {
            return ApiResponse.success(deadMessageService.query(queryDTO));
        } catch (Exception e) {
            log.error("查询死信消息列表失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/retry")
    @Operation(summary = "单条消息重试", description = "手动重试指定的死信消息")
    public ApiResponse<Void> retry(@RequestBody RetryRequestDTO requestDTO) {
        try {
            return deadMessageService.retry(requestDTO);
        } catch (Exception e) {
            log.error("重试消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/batch-retry")
    @Operation(summary = "批量消息重试", description = "批量重试指定的死信消息")
    public ApiResponse<Void> batchRetry(@RequestBody BatchRetryRequestDTO requestDTO) {
        try {
            return deadMessageService.batchRetry(requestDTO);
        } catch (Exception e) {
            log.error("批量重试消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/clean")
    @Operation(summary = "批量清理消息", description = "批量清理指定的死信消息")
    public ApiResponse<Void> clean(@RequestBody BatchCleanRequestDTO requestDTO) {
        try {
            return deadMessageService.clean(requestDTO);
        } catch (Exception e) {
            log.error("清理消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @DeleteMapping("/clean")
    @Operation(summary = "按状态清理消息", description = "根据队列名称和状态清理死信消息")
    public ApiResponse<Void> cleanByStatus(
            @Parameter(description = "队列名称") @RequestParam String queueName,
            @Parameter(description = "消息状态") @RequestParam String status) {
        try {
            return deadMessageService.cleanByStatus(queueName, status);
        } catch (Exception e) {
            log.error("清理消息失败: queueName={}, status={}", queueName, status, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/{id}/status")
    @Operation(summary = "更新消息状态", description = "更新死信消息的状态")
    public ApiResponse<Void> updateStatus(
            @Parameter(description = "死信消息ID") @PathVariable Long id,
            @Parameter(description = "新状态") @RequestParam String status) {
        try {
            return deadMessageService.updateStatus(id, status);
        } catch (Exception e) {
            log.error("更新消息状态失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/{id}/category")
    @Operation(summary = "更新消息分类", description = "更新死信消息的分类标签")
    public ApiResponse<Void> updateCategory(
            @Parameter(description = "死信消息ID") @PathVariable Long id,
            @Parameter(description = "分类标签") @RequestParam String category) {
        try {
            return deadMessageService.updateCategory(id, category);
        } catch (Exception e) {
            log.error("更新消息分类失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/batch-category")
    @Operation(summary = "批量更新消息分类", description = "批量更新死信消息的分类标签")
    public ApiResponse<Void> batchUpdateCategory(
            @Parameter(description = "消息ID列表") @RequestParam List<Long> ids,
            @Parameter(description = "分类标签") @RequestParam String category) {
        try {
            return deadMessageService.batchUpdateCategory(ids, category);
        } catch (Exception e) {
            log.error("批量更新消息分类失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/stats")
    @Operation(summary = "获取队列统计信息", description = "获取各队列的死信消息统计信息")
    public ApiResponse<List<QueueStatsDTO>> getQueueStats() {
        try {
            return ApiResponse.success(deadMessageService.getQueueStats());
        } catch (Exception e) {
            log.error("获取队列统计信息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/category-stats")
    @Operation(summary = "获取分类统计信息", description = "获取各分类的死信消息统计信息")
    public ApiResponse<List<CategoryStatsDTO>> getCategoryStats() {
        try {
            return ApiResponse.success(deadMessageService.getCategoryStats());
        } catch (Exception e) {
            log.error("获取分类统计信息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/error-type-stats")
    @Operation(summary = "获取错误类型统计", description = "获取各错误类型的死信消息统计信息")
    public ApiResponse<List<ErrorTypeStatsDTO>> getErrorTypeStats() {
        try {
            return ApiResponse.success(deadMessageService.getErrorTypeStats());
        } catch (Exception e) {
            log.error("获取错误类型统计失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/error-analysis")
    @Operation(summary = "获取异常原因分析", description = "获取全面的异常原因分析报告，包括分类、错误类型、错误码等统计")
    public ApiResponse<ErrorAnalysisDTO> getErrorAnalysis() {
        try {
            return ApiResponse.success(deadMessageService.getErrorAnalysis());
        } catch (Exception e) {
            log.error("获取异常原因分析失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/queue-error-stats")
    @Operation(summary = "获取队列错误统计", description = "获取各队列的错误统计信息")
    public ApiResponse<List<QueueErrorStatsDTO>> getQueueErrorStats() {
        try {
            return ApiResponse.success(deadMessageService.getQueueErrorStats());
        } catch (Exception e) {
            log.error("获取队列错误统计失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }
}
