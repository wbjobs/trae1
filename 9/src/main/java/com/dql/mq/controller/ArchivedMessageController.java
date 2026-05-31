package com.dql.mq.controller;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.ArchivedMessageDTO;
import com.dql.mq.dto.PageResult;
import com.dql.mq.service.ArchivedMessageService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@Slf4j
@RestController
@RequestMapping("/archived-messages")
@RequiredArgsConstructor
@Tag(name = "归档消息管理", description = "归档消息查询和清理接口")
public class ArchivedMessageController {

    private final ArchivedMessageService archivedMessageService;

    @GetMapping("/{id}")
    @Operation(summary = "根据ID查询归档消息", description = "根据ID查询归档消息详情")
    public ApiResponse<ArchivedMessageDTO> getById(
            @Parameter(description = "归档消息ID") @PathVariable Long id) {
        try {
            return ApiResponse.success(archivedMessageService.getById(id));
        } catch (Exception e) {
            log.error("查询归档消息失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/queue/{queueName}")
    @Operation(summary = "根据队列名称查询归档消息", description = "根据队列名称分页查询归档消息")
    public ApiResponse<PageResult<ArchivedMessageDTO>> getByQueueName(
            @Parameter(description = "队列名称") @PathVariable String queueName,
            @Parameter(description = "页码") @RequestParam(defaultValue = "0") int page,
            @Parameter(description = "每页大小") @RequestParam(defaultValue = "20") int size) {
        try {
            return ApiResponse.success(archivedMessageService.getByQueueName(queueName, page, size));
        } catch (Exception e) {
            log.error("查询归档消息失败: queueName={}", queueName, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/queue/{queueName}/status/{status}")
    @Operation(summary = "根据队列和状态查询归档消息", description = "根据队列名称和状态分页查询归档消息")
    public ApiResponse<PageResult<ArchivedMessageDTO>> getByQueueNameAndStatus(
            @Parameter(description = "队列名称") @PathVariable String queueName,
            @Parameter(description = "状态") @PathVariable String status,
            @Parameter(description = "页码") @RequestParam(defaultValue = "0") int page,
            @Parameter(description = "每页大小") @RequestParam(defaultValue = "20") int size) {
        try {
            return ApiResponse.success(archivedMessageService.getByQueueNameAndStatus(queueName, status, page, size));
        } catch (Exception e) {
            log.error("查询归档消息失败: queueName={}, status={}", queueName, status, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping
    @Operation(summary = "分页查询归档消息", description = "分页查询所有归档消息")
    public ApiResponse<PageResult<ArchivedMessageDTO>> getPage(
            @Parameter(description = "页码") @RequestParam(defaultValue = "0") int page,
            @Parameter(description = "每页大小") @RequestParam(defaultValue = "20") int size) {
        try {
            return ApiResponse.success(archivedMessageService.getPage(page, size));
        } catch (Exception e) {
            log.error("查询归档消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @DeleteMapping
    @Operation(summary = "批量删除归档消息", description = "根据ID列表批量删除归档消息")
    public ApiResponse<Void> deleteByIds(@RequestBody List<Long> ids) {
        try {
            return archivedMessageService.deleteByIds(ids);
        } catch (Exception e) {
            log.error("删除归档消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @DeleteMapping("/clean")
    @Operation(summary = "清理过期归档消息", description = "清理指定天数之前的归档消息")
    public ApiResponse<Void> cleanArchivedBeforeDays(
            @Parameter(description = "天数") @RequestParam Integer days) {
        try {
            return archivedMessageService.cleanArchivedBeforeDays(days);
        } catch (Exception e) {
            log.error("清理归档消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping("/archive")
    @Operation(summary = "手动归档死信消息", description = "将死信消息归档到归档表")
    public ApiResponse<Integer> archiveDeadMessages(
            @Parameter(description = "队列名称") @RequestParam(required = false) String queueName,
            @Parameter(description = "状态") @RequestParam(required = false) String status,
            @Parameter(description = "归档原因") @RequestParam(required = false) String reason) {
        try {
            return archivedMessageService.archiveDeadMessages(queueName, status, reason);
        } catch (Exception e) {
            log.error("归档死信消息失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }
}
