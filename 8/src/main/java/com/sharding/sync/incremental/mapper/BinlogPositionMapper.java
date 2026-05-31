package com.sharding.sync.incremental.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.sharding.sync.incremental.entity.BinlogPosition;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface BinlogPositionMapper extends BaseMapper<BinlogPosition> {
}
