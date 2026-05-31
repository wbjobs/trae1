package com.distributed.task.scheduler.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.entity.RetryStrategy;
import com.distributed.task.common.result.R;
import com.distributed.task.scheduler.service.RetryStrategyService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

@Api(tags = "重试策略配置")
@RestController
@RequestMapping("/retry-strategy")
@RequiredArgsConstructor
public class RetryStrategyController {

    private final RetryStrategyService retryStrategyService;

    @ApiOperation("按任务类型查询策略")
    @ApiImplicitParam(name = "taskType", value = "任务类型", required = true)
    @GetMapping("/{taskType}")
    public R<RetryStrategy> getByTaskType(@PathVariable String taskType) {
        return R.ok(retryStrategyService.getByTaskType(taskType));
    }

    @ApiOperation("分页查询策略")
    @ApiImplicitParams({
            @ApiImplicitParam(name = "current", value = "页码", defaultValue = "1"),
            @ApiImplicitParam(name = "size", value = "每页条数", defaultValue = "10"),
            @ApiImplicitParam(name = "taskType", value = "任务类型")
    })
    @GetMapping("/page")
    public R<Page<RetryStrategy>> page(@RequestParam(defaultValue = "1") int current,
                                       @RequestParam(defaultValue = "10") int size,
                                       @RequestParam(required = false) String taskType) {
        return R.ok(retryStrategyService.page(current, size, taskType));
    }

    @ApiOperation("新增或更新策略")
    @PostMapping("/save")
    public R<RetryStrategy> save(@RequestBody RetryStrategy strategy) {
        return R.ok(retryStrategyService.save(strategy));
    }

    @ApiOperation("删除策略")
    @ApiImplicitParam(name = "id", value = "策略ID", required = true)
    @DeleteMapping("/{id}")
    public R<Boolean> delete(@PathVariable Long id) {
        return R.ok(retryStrategyService.delete(id));
    }

    @ApiOperation("手动刷新缓存")
    @PostMapping("/refresh")
    public R<Boolean> refresh() {
        retryStrategyService.refreshCache();
        return R.ok(true);
    }
}
