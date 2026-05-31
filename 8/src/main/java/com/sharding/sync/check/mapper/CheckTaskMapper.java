package com.sharding.sync.check.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.sharding.sync.check.entity.CheckTask;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface CheckTaskMapper extends BaseMapper<CheckTask> {
}
