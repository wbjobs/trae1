package com.sharding.sync.controller;

import com.sharding.sync.common.Result;
import com.sharding.sync.query.service.CrossShardQueryService;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/query")
@RequiredArgsConstructor
public class CrossShardQueryController {

    private final CrossShardQueryService crossShardQueryService;

    @PostMapping("/cross-shard")
    public Result<Map<String, Object>> crossShardQuery(@RequestBody Map<String, Object> body) {
        String logicTable = (String) body.get("logicTable");
        String sql = (String) body.get("sql");
        if (logicTable == null || sql == null) {
            return Result.fail("logicTable 和 sql 必填");
        }
        List<Object> params = (List<Object>) body.get("params");
        Integer page = body.get("page") == null ? null : ((Number) body.get("page")).intValue();
        Integer size = body.get("size") == null ? null : ((Number) body.get("size")).intValue();
        return Result.success(crossShardQueryService.query(logicTable, sql, params, page, size));
    }

    @PostMapping("/aggregate")
    public Result<Map<String, Object>> aggregate(@RequestBody Map<String, Object> body) {
        String logicTable = (String) body.get("logicTable");
        String sql = (String) body.get("sql");
        if (logicTable == null || sql == null) {
            return Result.fail("logicTable 和 sql 必填");
        }
        List<Object> params = (List<Object>) body.get("params");
        return Result.success(crossShardQueryService.aggregate(logicTable, sql, params));
    }

    @PostMapping("/group-by")
    public Result<Map<String, Object>> groupBy(@RequestBody Map<String, Object> body) {
        String logicTable = (String) body.get("logicTable");
        String sql = (String) body.get("sql");
        String groupColumn = (String) body.get("groupColumn");
        if (logicTable == null || sql == null || groupColumn == null) {
            return Result.fail("logicTable、sql 和 groupColumn 必填");
        }
        List<Object> params = (List<Object>) body.get("params");
        List<String> aggregateColumns = (List<String>) body.get("aggregateColumns");
        return Result.success(crossShardQueryService.groupBy(logicTable, sql, params, groupColumn, aggregateColumns));
    }

    @PostMapping("/by-shard")
    public Result<Map<String, Object>> queryByShardColumn(@RequestBody Map<String, Object> body) {
        String logicTable = (String) body.get("logicTable");
        String sql = (String) body.get("sql");
        Object shardValue = body.get("shardValue");
        if (logicTable == null || sql == null || shardValue == null) {
            return Result.fail("logicTable、sql 和 shardValue 必填");
        }
        List<Object> params = (List<Object>) body.get("params");
        return Result.success(crossShardQueryService.queryByShardColumn(logicTable, shardValue, sql, params));
    }

    @GetMapping("/stats/{logicTable}")
    public Result<Map<String, Map<String, Object>>> stats(@PathVariable String logicTable) {
        return Result.success(crossShardQueryService.queryAllShardsStats(logicTable));
    }
}
