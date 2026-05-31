package com.sharding.sync.shard.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.sharding.sync.shard.entity.ShardRule;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface ShardRuleMapper extends BaseMapper<ShardRule> {
}
