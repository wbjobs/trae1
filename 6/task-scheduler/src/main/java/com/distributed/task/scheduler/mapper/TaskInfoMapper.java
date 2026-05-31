package com.distributed.task.scheduler.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.distributed.task.common.entity.TaskInfo;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface TaskInfoMapper extends BaseMapper<TaskInfo> {
}
