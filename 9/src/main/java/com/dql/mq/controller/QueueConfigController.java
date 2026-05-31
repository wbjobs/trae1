package com.dql.mq.controller;

import com.dql.mq.dto.ApiResponse;
import com.dql.mq.dto.PageResult;
import com.dql.mq.dto.QueueConfigDTO;
import com.dql.mq.service.QueueConfigService;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;
import jakarta.validation.Valid;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@Slf4j
@RestController
@RequestMapping("/queue-configs")
@RequiredArgsConstructor
@Tag(name = "队列配置管理", description = "队列配置的增删改查接口")
public class QueueConfigController {

    private final QueueConfigService queueConfigService;

    @GetMapping("/{id}")
    @Operation(summary = "根据ID查询队列配置", description = "根据ID查询队列配置详情")
    public ApiResponse<QueueConfigDTO> getById(
            @Parameter(description = "队列配置ID") @PathVariable Long id) {
        try {
            return ApiResponse.success(queueConfigService.getById(id));
        } catch (Exception e) {
            log.error("查询队列配置失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/queue/{queueName}")
    @Operation(summary = "根据队列名称查询配置", description = "根据队列名称查询队列配置详情")
    public ApiResponse<QueueConfigDTO> getByQueueName(
            @Parameter(description = "队列名称") @PathVariable String queueName) {
        try {
            return ApiResponse.success(queueConfigService.getByQueueName(queueName));
        } catch (Exception e) {
            log.error("查询队列配置失败: queueName={}", queueName, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping
    @Operation(summary = "查询所有队列配置", description = "查询所有队列配置列表")
    public ApiResponse<List<QueueConfigDTO>> getAll() {
        try {
            return ApiResponse.success(queueConfigService.getAll());
        } catch (Exception e) {
            log.error("查询队列配置列表失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @GetMapping("/page")
    @Operation(summary = "分页查询队列配置", description = "分页查询队列配置列表")
    public ApiResponse<PageResult<QueueConfigDTO>> getPage(
            @Parameter(description = "页码") @RequestParam(defaultValue = "0") int page,
            @Parameter(description = "每页大小") @RequestParam(defaultValue = "20") int size) {
        try {
            return ApiResponse.success(queueConfigService.getPage(page, size));
        } catch (Exception e) {
            log.error("分页查询队列配置失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PostMapping
    @Operation(summary = "创建队列配置", description = "创建新的队列配置")
    public ApiResponse<QueueConfigDTO> create(@Valid @RequestBody QueueConfigDTO dto) {
        try {
            return queueConfigService.create(dto);
        } catch (Exception e) {
            log.error("创建队列配置失败", e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/{id}")
    @Operation(summary = "更新队列配置", description = "更新指定的队列配置")
    public ApiResponse<QueueConfigDTO> update(
            @Parameter(description = "队列配置ID") @PathVariable Long id,
            @Valid @RequestBody QueueConfigDTO dto) {
        try {
            return queueConfigService.update(id, dto);
        } catch (Exception e) {
            log.error("更新队列配置失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @DeleteMapping("/{id}")
    @Operation(summary = "删除队列配置", description = "删除指定的队列配置")
    public ApiResponse<Void> delete(
            @Parameter(description = "队列配置ID") @PathVariable Long id) {
        try {
            return queueConfigService.delete(id);
        } catch (Exception e) {
            log.error("删除队列配置失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/{id}/enabled")
    @Operation(summary = "启用/禁用队列", description = "启用或禁用队列的消息监听")
    public ApiResponse<Void> updateEnabled(
            @Parameter(description = "队列配置ID") @PathVariable Long id,
            @Parameter(description = "是否启用") @RequestParam Boolean enabled) {
        try {
            return queueConfigService.updateEnabled(id, enabled);
        } catch (Exception e) {
            log.error("更新队列启用状态失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }

    @PutMapping("/{id}/max-retry-count")
    @Operation(summary = "更新最大重试次数", description = "更新队列的最大重试次数配置")
    public ApiResponse<Void> updateMaxRetryCount(
            @Parameter(description = "队列配置ID") @PathVariable Long id,
            @Parameter(description = "最大重试次数") @RequestParam Integer maxRetryCount) {
        try {
            return queueConfigService.updateMaxRetryCount(id, maxRetryCount);
        } catch (Exception e) {
            log.error("更新最大重试次数失败: id={}", id, e);
            return ApiResponse.error(e.getMessage());
        }
    }
}
