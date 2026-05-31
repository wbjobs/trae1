package com.distributed.task.alarm.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.distributed.task.common.entity.AlarmRecord;
import org.apache.ibatis.annotations.Mapper;

@Mapper
public interface AlarmRecordMapper extends BaseMapper<AlarmRecord> {
}
