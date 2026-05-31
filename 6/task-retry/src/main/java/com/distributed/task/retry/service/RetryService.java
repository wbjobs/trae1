package com.distributed.task.retry.service;

import com.distributed.task.common.dto.TaskVO;

import java.util.List;

public interface RetryService {

    List<TaskVO> scanDueTasks();

    boolean triggerRetry(String taskNo);

    boolean claimAndDispatch(String taskNo, String workerNode);

    String claim(String workerNode);
}
