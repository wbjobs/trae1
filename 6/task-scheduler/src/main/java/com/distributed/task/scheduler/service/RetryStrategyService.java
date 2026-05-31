package com.distributed.task.scheduler.service;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.entity.RetryStrategy;

public interface RetryStrategyService {

    RetryStrategy getByTaskType(String taskType);

    Page<RetryStrategy> page(int current, int size, String taskType);

    RetryStrategy save(RetryStrategy strategy);

    boolean delete(Long id);

    void refreshCache();
}
