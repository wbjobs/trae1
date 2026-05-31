package com.cdn.api.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.common.result.R;
import com.cdn.domain.dto.BatchRefreshDTO;
import com.cdn.domain.entity.RefreshLog;
import com.cdn.service.RefreshService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import javax.validation.Valid;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

@Api(tags = "刷新管理")
@RestController
@RequestMapping("/api/refresh")
@RequiredArgsConstructor
public class RefreshController {

    private final RefreshService refreshService;

    @ApiOperation("批量刷新资源")
    @PostMapping("/batch")
    public R<Map<String, Object>> batchRefresh(@Valid @RequestBody BatchRefreshDTO dto) {
        try {
            CompletableFuture<Map<String, Object>> future = refreshService.batchRefresh(dto);
            Map<String, Object> result = future.get(30, TimeUnit.SECONDS);
            return R.ok(result);
        } catch (java.util.concurrent.TimeoutException e) {
            return R.fail(504, "刷新任务超时，请稍后查询任务状态");
        } catch (Exception e) {
            return R.fail(500, "刷新任务异常: " + e.getMessage());
        }
    }

    @ApiOperation("查询刷新任务状态")
    @GetMapping("/task/{taskId}")
    public R<Map<String, Object>> taskStatus(@PathVariable String taskId) {
        return R.ok(refreshService.getTaskStatus(taskId));
    }

    @ApiOperation("分页查询刷新日志")
    @GetMapping("/log/page")
    public R<Page<RefreshLog>> logPage(@RequestParam(defaultValue = "1") int pageNum,
                                       @RequestParam(defaultValue = "10") int pageSize,
                                       @RequestParam(required = false) String keyword) {
        return R.ok(refreshService.logPage(pageNum, pageSize, keyword));
    }
}
