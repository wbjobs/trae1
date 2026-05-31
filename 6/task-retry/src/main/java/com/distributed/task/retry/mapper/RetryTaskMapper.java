package com.distributed.task.retry.mapper;

import com.baomidou.mybatisplus.core.mapper.BaseMapper;
import com.distributed.task.common.entity.TaskInfo;
import org.apache.ibatis.annotations.Mapper;
import org.apache.ibatis.annotations.Param;
import org.apache.ibatis.annotations.Select;
import org.apache.ibatis.annotations.Update;

import java.time.LocalDateTime;
import java.util.List;

@Mapper
public interface RetryTaskMapper extends BaseMapper<TaskInfo> {

    @Select("SELECT id, task_no, task_type, biz_key, task_payload, callback_url, status, " +
            "retry_count, max_retry_count, retry_level, timeout_seconds, next_retry_time, " +
            "first_execute_time, last_execute_time, owner_node, remark, create_time, update_time " +
            "FROM t_task_info WHERE deleted = 0 AND status IN (0, 4) " +
            "AND (status = 0 OR next_retry_time <= #{now}) " +
            "ORDER BY CASE WHEN status = 0 THEN create_time ELSE next_retry_time END ASC " +
            "LIMIT #{limit}")
    List<TaskInfo> selectDueTasks(@Param("now") LocalDateTime now, @Param("limit") int limit);

    @Update("UPDATE t_task_info SET status = 7 WHERE id = #{id} AND status IN (0, 4)")
    int markQueued(@Param("id") Long id);
}
