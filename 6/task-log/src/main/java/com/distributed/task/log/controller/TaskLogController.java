package com.distributed.task.log.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.entity.TaskLog;
import com.distributed.task.common.result.R;
import com.distributed.task.log.service.TaskLogService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.Map;

@Api(tags = "任务日志")
@RestController
@RequestMapping("/log")
@RequiredArgsConstructor
public class TaskLogController {

    private final TaskLogService taskLogService;

    @ApiOperation("记录执行日志（内部）")
    @PostMapping("/record")
    public R<Map<String, Long>> record(@RequestBody TaskExecuteDTO dto,
                                       @RequestParam(value = "retryNo", required = false) Integer retryNo) {
        Long id = taskLogService.save(dto, retryNo);
        Map<String, Long> data = new HashMap<>(2);
        data.put("logId", id);
        return R.ok(data);
    }

    @ApiOperation("按任务编号分页查询日志")
    @ApiImplicitParams({
            @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true),
            @ApiImplicitParam(name = "current", value = "页码", defaultValue = "1"),
            @ApiImplicitParam(name = "size", value = "每页条数", defaultValue = "10")
    })
    @GetMapping("/task/{taskNo}")
    public R<Page<TaskLog>> pageByTaskNo(@PathVariable String taskNo,
                                         @RequestParam(defaultValue = "1") int current,
                                         @RequestParam(defaultValue = "10") int size) {
        return R.ok(taskLogService.pageByTaskNo(taskNo, current, size));
    }

    @ApiOperation("全量分页查询日志")
    @ApiImplicitParams({
            @ApiImplicitParam(name = "current", value = "页码", defaultValue = "1"),
            @ApiImplicitParam(name = "size", value = "每页条数", defaultValue = "10"),
            @ApiImplicitParam(name = "taskType", value = "任务类型"),
            @ApiImplicitParam(name = "executeStatus", value = "执行状态 1成功 0失败")
    })
    @GetMapping("/page")
    public R<Page<TaskLog>> page(@RequestParam(defaultValue = "1") int current,
                                 @RequestParam(defaultValue = "10") int size,
                                 @RequestParam(required = false) String taskType,
                                 @RequestParam(required = false) Integer executeStatus) {
        return R.ok(taskLogService.page(current, size, taskType, executeStatus));
    }

    @ApiOperation("查询某任务全部日志（内部）")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @GetMapping("/list-by-task")
    public R<List<TaskLog>> listByTaskNo(@RequestParam String taskNo) {
        return R.ok(taskLogService.listByTaskNo(taskNo));
    }
}
