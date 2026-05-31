package com.distributed.task.retry.controller;

import com.distributed.task.common.dto.TaskVO;
import com.distributed.task.common.result.R;
import com.distributed.task.retry.service.RetryService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Api(tags = "重试机制")
@RestController
@RequestMapping("/retry")
@RequiredArgsConstructor
public class RetryController {

    private final RetryService retryService;

    @ApiOperation("扫描到期重试任务并分发")
    @PostMapping("/scan")
    public R<List<TaskVO>> scan() {
        return R.ok(retryService.scanDueTasks());
    }

    @ApiOperation("手动触发重试")
    @ApiImplicitParam(name = "taskNo", value = "任务编号", required = true)
    @PostMapping("/trigger/{taskNo}")
    public R<Boolean> triggerRetry(@PathVariable String taskNo) {
        return R.ok(retryService.triggerRetry(taskNo));
    }

    @ApiOperation("工作节点领取任务")
    @ApiImplicitParam(name = "workerNode", value = "工作节点ID", required = true)
    @PostMapping("/claim")
    public R<Map<String, String>> claim(@RequestParam String workerNode) {
        String taskNo = retryService.claim(workerNode);
        Map<String, String> data = new HashMap<>(2);
        data.put("taskNo", taskNo);
        return R.ok(data);
    }
}
