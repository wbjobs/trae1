package com.sharding.sync.sync.service;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.sync.dto.SyncTaskDTO;
import com.sharding.sync.sync.entity.SyncTask;

import java.util.List;
import java.util.Map;

public interface SyncTaskService {

    SyncTask submit(SyncTaskDTO dto);

    SyncTask getById(Long id);

    SyncTask getByTaskNo(String taskNo);

    IPage<SyncTask> page(Page<SyncTask> page, String logicTable, String syncType, String status);

    List<SyncTask> listRecent(String logicTable, int limit);

    Map<String, Object> getStatus(String taskNo);

    boolean cancel(String taskNo);

    void updateProgress(Long taskId, long success, long fail, String status);

    void finishTask(Long taskId, String status, String errorMsg);
}
