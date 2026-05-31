package com.sharding.sync.sync.service.impl;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.metadata.IPage;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.baomidou.mybatisplus.extension.service.impl.ServiceImpl;
import com.sharding.sync.common.BusinessException;
import com.sharding.sync.common.ResultCode;
import com.sharding.sync.common.SyncStatus;
import com.sharding.sync.common.SyncType;
import com.sharding.sync.sync.dto.SyncTaskDTO;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.mapper.SyncTaskMapper;
import com.sharding.sync.sync.service.SyncTaskService;
import com.alibaba.fastjson2.JSON;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.util.StringUtils;

import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

@Slf4j
@Service
@RequiredArgsConstructor
public class SyncTaskServiceImpl extends ServiceImpl<SyncTaskMapper, SyncTask> implements SyncTaskService {

    @Override
    public SyncTask submit(SyncTaskDTO dto) {
        SyncTask task = new SyncTask();
        task.setTaskNo(UUID.randomUUID().toString().replace("-", ""));
        task.setTaskName(dto.getLogicTable() + "_" + dto.getSyncType() + "_" + System.currentTimeMillis());
        task.setSyncType(dto.getSyncType());
        task.setLogicTable(dto.getLogicTable());
        task.setSourceDs(dto.getSourceDs());
        task.setTargetDs(dto.getTargetDs());
        task.setStatus(SyncStatus.PENDING.getCode());
        task.setTriggerMode(dto.getTriggerMode());
        task.setTotalCount(0L);
        task.setSuccessCount(0L);
        task.setFailCount(0L);
        task.setParams(dto.getParams() == null ? null : JSON.toJSONString(dto.getParams()));
        task.setCreateTime(LocalDateTime.now());
        task.setUpdateTime(LocalDateTime.now());
        save(task);
        return task;
    }

    @Override
    public SyncTask getById(Long id) {
        return getBaseMapper().selectById(id);
    }

    @Override
    public SyncTask getByTaskNo(String taskNo) {
        return getOne(new LambdaQueryWrapper<SyncTask>().eq(SyncTask::getTaskNo, taskNo));
    }

    @Override
    public IPage<SyncTask> page(Page<SyncTask> page, String logicTable, String syncType, String status) {
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        if (StringUtils.hasText(logicTable)) {
            wrapper.eq(SyncTask::getLogicTable, logicTable);
        }
        if (StringUtils.hasText(syncType)) {
            wrapper.eq(SyncTask::getSyncType, syncType);
        }
        if (StringUtils.hasText(status)) {
            wrapper.eq(SyncTask::getStatus, status);
        }
        wrapper.orderByDesc(SyncTask::getCreateTime);
        return page(page, wrapper);
    }

    @Override
    public List<SyncTask> listRecent(String logicTable, int limit) {
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        if (StringUtils.hasText(logicTable)) {
            wrapper.eq(SyncTask::getLogicTable, logicTable);
        }
        wrapper.orderByDesc(SyncTask::getCreateTime);
        wrapper.last("limit " + limit);
        return list(wrapper);
    }

    @Override
    public Map<String, Object> getStatus(String taskNo) {
        SyncTask task = getByTaskNo(taskNo);
        if (task == null) {
            throw new BusinessException(ResultCode.SYNC_NOT_FOUND);
        }
        Map<String, Object> result = new HashMap<>();
        result.put("taskNo", task.getTaskNo());
        result.put("logicTable", task.getLogicTable());
        result.put("syncType", task.getSyncType());
        result.put("status", task.getStatus());
        result.put("totalCount", task.getTotalCount());
        result.put("successCount", task.getSuccessCount());
        result.put("failCount", task.getFailCount());
        result.put("startTime", task.getStartTime());
        result.put("endTime", task.getEndTime());
        result.put("triggerMode", task.getTriggerMode());
        result.put("errorMsg", task.getErrorMsg());
        return result;
    }

    @Override
    public boolean cancel(String taskNo) {
        SyncTask task = getByTaskNo(taskNo);
        if (task == null) {
            throw new BusinessException(ResultCode.SYNC_NOT_FOUND);
        }
        if (SyncStatus.RUNNING.getCode().equals(task.getStatus()) || SyncStatus.PENDING.getCode().equals(task.getStatus())) {
            task.setStatus(SyncStatus.CANCELED.getCode());
            task.setEndTime(LocalDateTime.now());
            task.setUpdateTime(LocalDateTime.now());
            updateById(task);
            return true;
        }
        return false;
    }

    @Override
    public void updateProgress(Long taskId, long success, long fail, String status) {
        SyncTask task = getById(taskId);
        if (task == null) {
            return;
        }
        task.setSuccessCount(success);
        task.setFailCount(fail);
        task.setTotalCount(success + fail);
        if (StringUtils.hasText(status)) {
            task.setStatus(status);
        }
        task.setUpdateTime(LocalDateTime.now());
        updateById(task);
    }

    @Override
    public void finishTask(Long taskId, String status, String errorMsg) {
        SyncTask task = getById(taskId);
        if (task == null) {
            return;
        }
        task.setStatus(status);
        task.setEndTime(LocalDateTime.now());
        task.setErrorMsg(errorMsg);
        task.setUpdateTime(LocalDateTime.now());
        updateById(task);
    }
}
