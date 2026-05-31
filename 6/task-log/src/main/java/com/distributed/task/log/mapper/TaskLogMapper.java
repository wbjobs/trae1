package com.distributed.task.log.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.distributed.task.common.entity.TaskLog;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface TaskLogMapper extends BaseMapper<TaskLog> {
}
