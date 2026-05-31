package com.distributed.task.scheduler.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.dto.TaskSubmitDTO;
import com.distributed.task.common.dto.TaskVO;
import com.distributed.task.common.result.R;
import com.distributed.task.scheduler.service.TaskService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.Map;

@Api(tags = "任务调度")
@RestController
@RequestMapping("/task")
@RequiredArgsConstructor
public class TaskController {

    private final TaskService taskService;

    @ApiOperation("提交任务")
    @PostMapping("/submit")
    public R<Map<String, String>> submit(@Validated @RequestBody TaskSubmitDTO dto) {
        String taskNo = taskService.submit(dto);
        Map<String, String> data = new HashMap<>(2);
        data.put("taskNo", taskNo);
        return R.ok(data);
    }

    @ApiOperation("查询任务")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @GetMapping("/{taskNo}")
    public R<TaskVO> getByTaskNo(@PathVariable String taskNo) {
        return R.ok(taskService.getByTaskNo(taskNo));
    }

    @ApiOperation("任务分页")
    @ApiImplicitParams({
            @ApiImplicitParam(name = "current", value = "页码", defaultValue = "1"),
            @ApiImplicitParam(name = "size", value = "每页条数", defaultValue = "10"),
            @ApiImplicitParam(name = "taskType", value = "任务类型"),
            @ApiImplicitParam(name = "status", value = "任务状态")
    })
    @GetMapping("/page")
    public R<Page<TaskVO>> page(@RequestParam(defaultValue = "1") int current,
                                @RequestParam(defaultValue = "10") int size,
                                @RequestParam(required = false) String taskType,
                                @RequestParam(required = false) Integer status) {
        return R.ok(taskService.page(current, size, taskType, status));
    }

    @ApiOperation("取消任务")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @PostMapping("/cancel/{taskNo}")
    public R<Boolean> cancel(@PathVariable String taskNo) {
        return R.ok(taskService.cancel(taskNo));
    }

    @ApiOperation("上报执行结果（内部）")
    @PostMapping("/execute/result")
    public R<Boolean> handleExecuteResult(@RequestBody TaskExecuteDTO dto) {
        return R.ok(taskService.handleExecuteResult(dto));
    }
}
