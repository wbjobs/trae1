package com.sharding.sync.check.service;

import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.sharding.sync.check.entity.CheckDiff;
import com.sharding.sync.check.entity.CheckTask;

import java.util.List;
import java.util.Map;

public interface CheckService {

    CheckTask submit(String logicTable, String checkType);

    CheckTask getByTaskNo(String taskNo);

    IPage<CheckTask> page(Page<CheckTask> page, String logicTable, String status);

    List<CheckDiff> listDiffs(Long taskId);

    List<CheckDiff> listPendingFixes(String logicTable, int limit);

    Map<String, Object> getStatus(String taskNo);

    void runCheck(CheckTask task);
}
