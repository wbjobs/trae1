package com.sharding.sync.controller;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.common.Result;
import com.sharding.sync.shard.dto.ShardRuleDTO;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/api/shard-rules")
@RequiredArgsConstructor
public class ShardRuleController {

    private final ShardRuleService shardRuleService;

    @PostMapping
    public Result<ShardRule> create(@Validated @RequestBody ShardRuleDTO dto) {
        return Result.success(shardRuleService.save(dto));
    }

    @PutMapping
    public Result<ShardRule> update(@Validated @RequestBody ShardRuleDTO dto) {
        return Result.success(shardRuleService.update(dto));
    }

    @DeleteMapping("/{id}")
    public Result<Void> delete(@PathVariable Long id) {
        shardRuleService.delete(id);
        return Result.success();
    }

    @GetMapping("/{id}")
    public Result<ShardRule> get(@PathVariable Long id) {
        return Result.success(shardRuleService.getById(id));
    }

    @GetMapping("/by-table/{logicTable}")
    public Result<ShardRule> getByLogicTable(@PathVariable String logicTable) {
        return Result.success(shardRuleService.getByLogicTable(logicTable));
    }

    @GetMapping("/list")
    public Result<List<ShardRule>> list() {
        return Result.success(shardRuleService.listAll());
    }

    @GetMapping("/page")
    public Result<IPage<ShardRule>> page(@RequestParam(defaultValue = "1") Integer current,
                                         @RequestParam(defaultValue = "10") Integer size,
                                         @RequestParam(required = false) String keyword) {
        return Result.success(shardRuleService.page(new Page<>(current, size), keyword));
    }

    @PostMapping("/refresh")
    public Result<Void> refresh() {
        shardRuleService.refreshCache();
        return Result.success();
    }
}
