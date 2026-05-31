package com.distributed.task.log.service;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.entity.TaskLog;

public interface TaskLogService {

    Long save(TaskExecuteDTO dto, Integer retryNo);

    Page<TaskLog> pageByTaskNo(String taskNo, int current, int size);

    Page<TaskLog> page(int current, int size, String taskType, Integer executeStatus);

    List<TaskLog> listByTaskNo(String taskNo);
}
