package com.sharding.sync.controller;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.check.entity.CheckDiff;
import com.sharding.sync.check.entity.CheckTask;
import com.sharding.sync.check.service.CheckService;
import com.sharding.sync.common.Result;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/check")
@RequiredArgsConstructor
public class CheckController {

    private final CheckService checkService;

    @PostMapping("/submit")
    public Result<CheckTask> submit(@RequestParam String logicTable,
                                    @RequestParam(defaultValue = "ALL") String checkType) {
        return Result.success(checkService.submit(logicTable, checkType));
    }

    @GetMapping("/status/{taskNo}")
    public Result<Map<String, Object>> status(@PathVariable String taskNo) {
        return Result.success(checkService.getStatus(taskNo));
    }

    @GetMapping("/page")
    public Result<IPage<CheckTask>> page(@RequestParam(defaultValue = "1") Integer current,
                                         @RequestParam(defaultValue = "10") Integer size,
                                         @RequestParam(required = false) String logicTable,
                                         @RequestParam(required = false) String status) {
        return Result.success(checkService.page(new Page<>(current, size), logicTable, status));
    }

    @GetMapping("/diffs/{taskId}")
    public Result<List<CheckDiff>> listDiffs(@PathVariable Long taskId) {
        return Result.success(checkService.listDiffs(taskId));
    }

    @GetMapping("/pending-fixes")
    public Result<List<CheckDiff>> pendingFixes(@RequestParam String logicTable,
                                                @RequestParam(defaultValue = "100") Integer limit) {
        return Result.success(checkService.listPendingFixes(logicTable, limit));
    }
}
