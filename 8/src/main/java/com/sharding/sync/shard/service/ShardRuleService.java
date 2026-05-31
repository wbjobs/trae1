package com.sharding.sync.shard.service;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.shard.dto.ShardRuleDTO;
import com.sharding.sync.shard.entity.ShardRule;

import java.util.List;

public interface ShardRuleService {

    ShardRule save(ShardRuleDTO dto);

    ShardRule update(ShardRuleDTO dto);

    void delete(Long id);

    ShardRule getById(Long id);

    ShardRule getByLogicTable(String logicTable);

    List<ShardRule> listAll();

    IPage<ShardRule> page(Page<ShardRule> page, String keyword);

    void refreshCache();
}
