package com.sharding.sync.incremental.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.sharding.sync.incremental.entity.IncrementalEvent;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface IncrementalEventMapper extends BaseMapper<IncrementalEvent> {
}
