package com.sharding.sync.sync.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.sharding.sync.sync.entity.SyncTask;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface SyncTaskMapper extends BaseMapper<SyncTask> {
}
