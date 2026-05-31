package com.distributed.task.log.service.impl;

import cn.hutool.core.util.StrUtil;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.entity.TaskLog;
import com.distributed.task.log.mapper.TaskLogMapper;
import com.distributed.task.log.service.TaskLogService;
import lombok.RequiredArgsConstructor;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;

@Service
@RequiredArgsConstructor
public class TaskLogServiceImpl implements TaskLogService {

    private final TaskLogMapper taskLogMapper;

    @Override
    public Long save(TaskExecuteDTO dto, Integer retryNo) {
        TaskLog log = new TaskLog();
        log.setTaskNo(dto.getTaskNo());
        log.setExecuteNode(dto.getExecuteNode());
        log.setExecuteStatus(dto.getSuccess());
        log.setExecuteResult(StrUtil.maxLength(dto.getResult(), 2000));
        log.setErrorMessage(StrUtil.maxLength(dto.getErrorMessage(), 2000));
        log.setCostMs(dto.getCostMs());
        log.setRetryNo(retryNo);
        log.setCreateTime(LocalDateTime.now());
        taskLogMapper.insert(log);
        return log.getId();
    }

    @Override
    public Page<TaskLog> pageByTaskNo(String taskNo, int current, int size) {
        LambdaQueryWrapper<TaskLog> qw = new LambdaQueryWrapper<>();
        qw.eq(TaskLog::getTaskNo, taskNo).orderByDesc(TaskLog::getId);
        return taskLogMapper.selectPage(new Page<>(current, size), qw);
    }

    @Override
    public Page<TaskLog> page(int current, int size, String taskType, Integer executeStatus) {
        LambdaQueryWrapper<TaskLog> qw = new LambdaQueryWrapper<>();
        if (StrUtil.isNotBlank(taskType)) {
            qw.eq(TaskLog::getTaskType, taskType);
        }
        if (executeStatus != null) {
            qw.eq(TaskLog::getExecuteStatus, executeStatus);
        }
        qw.orderByDesc(TaskLog::getId);
        return taskLogMapper.selectPage(new Page<>(current, size), qw);
    }

    @Override
    public java.util.List<TaskLog> listByTaskNo(String taskNo) {
        return taskLogMapper.selectList(
                new LambdaQueryWrapper<TaskLog>()
                        .eq(TaskLog::getTaskNo, taskNo)
                        .orderByAsc(TaskLog::getId));
    }
}
