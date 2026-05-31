package com.distributed.task.scheduler.service;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.dto.TaskSubmitDTO;
import com.distributed.task.common.dto.TaskVO;

public interface TaskService {

    String submit(TaskSubmitDTO dto);

    TaskVO getByTaskNo(String taskNo);

    Page<TaskVO> page(int current, int size, String taskType, Integer status);

    boolean cancel(String taskNo);

    boolean handleExecuteResult(TaskExecuteDTO dto);

    boolean markRunning(Long taskId, String ownerNode);

    boolean markSuccess(Long taskId, String result);

    boolean markFailed(Long taskId, String errorMessage);

    boolean markTimeout(Long taskId);
}
