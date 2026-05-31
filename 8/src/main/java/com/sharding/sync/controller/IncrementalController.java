package com.sharding.sync.controller;

import com.sharding.sync.common.Result;
import com.sharding.sync.incremental.entity.BinlogPosition;
import com.sharding.sync.incremental.entity.IncrementalEvent;
import com.sharding.sync.incremental.service.IncrementalService;
import com.sharding.sync.incremental.service.IncrementalSyncWorker;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/incremental")
@RequiredArgsConstructor
public class IncrementalController {

    private final IncrementalService incrementalService;
    private final IncrementalSyncWorker incrementalSyncWorker;

    @GetMapping("/position/{logicTable}")
    public Result<BinlogPosition> position(@PathVariable String logicTable) {
        return Result.success(incrementalService.getPosition(logicTable));
    }

    @GetMapping("/events")
    public Result<List<IncrementalEvent>> events(@RequestParam String logicTable,
                                                 @RequestParam(defaultValue = "50") Integer limit) {
        return Result.success(incrementalService.listRecent(logicTable, limit));
    }

    @PostMapping("/events")
    public Result<Void> appendEvent(@RequestBody Map<String, Object> body) {
        incrementalSyncWorker.appendEvent(
                String.valueOf(body.get("logicTable")),
                String.valueOf(body.get("action")),
                String.valueOf(body.get("pkValue")),
                body.get("beforeData") == null ? null : com.alibaba.fastjson2.JSON.toJSONString(body.get("beforeData")),
                body.get("afterData") == null ? null : com.alibaba.fastjson2.JSON.toJSONString(body.get("afterData")),
                body.get("binlogFile") == null ? null : String.valueOf(body.get("binlogFile")),
                body.get("binlogPosition") == null ? null : ((Number) body.get("binlogPosition")).longValue());
        return Result.success();
    }

    @PostMapping("/mark-success/{id}")
    public Result<Void> markSuccess(@PathVariable Long id) {
        incrementalService.markEventSuccess(id);
        return Result.success();
    }

    @PostMapping("/mark-failed/{id}")
    public Result<Void> markFailed(@PathVariable Long id, @RequestParam String errorMsg) {
        incrementalService.markEventFailed(id, errorMsg);
        return Result.success();
    }
}
