package com.sharding.sync.controller;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.common.Result;
import com.sharding.sync.fix.entity.FixTask;
import com.sharding.sync.fix.service.FixService;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.Map;

@RestController
@RequestMapping("/api/fix")
@RequiredArgsConstructor
public class FixController {

    private final FixService fixService;

    @PostMapping("/submit")
    public Result<FixTask> submit(@RequestParam String logicTable,
                                  @RequestParam(required = false) Long checkTaskId) {
        return Result.success(fixService.submit(logicTable, checkTaskId));
    }

    @GetMapping("/status/{taskNo}")
    public Result<Map<String, Object>> status(@PathVariable String taskNo) {
        return Result.success(fixService.getStatus(taskNo));
    }

    @GetMapping("/page")
    public Result<IPage<FixTask>> page(@RequestParam(defaultValue = "1") Integer current,
                                       @RequestParam(defaultValue = "10") Integer size,
                                       @RequestParam(required = false) String logicTable,
                                       @RequestParam(required = false) String status) {
        return Result.success(fixService.page(new Page<>(current, size), logicTable, status));
    }
}
