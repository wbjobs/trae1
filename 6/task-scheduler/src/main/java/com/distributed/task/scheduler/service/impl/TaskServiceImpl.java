package com.distributed.task.scheduler.service.impl;

import cn.hutool.core.bean.BeanUtil;
import cn.hutool.core.util.StrUtil;
import com.alibaba.fastjson2.JSON;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.conditions.update.LambdaUpdateWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.dto.AlarmDTO;
import com.distributed.task.common.dto.IdempotentDTO;
import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.dto.TaskSubmitDTO;
import com.distributed.task.common.dto.TaskVO;
import com.distributed.task.common.entity.RetryStrategy;
import com.distributed.task.common.entity.TaskInfo;
import com.distributed.task.common.enums.RetryLevel;
import com.distributed.task.common.enums.TaskPriority;
import com.distributed.task.common.enums.TaskStatus;
import com.distributed.task.common.feign.AlarmClient;
import com.distributed.task.common.feign.IdempotentClient;
import com.distributed.task.common.result.R;
import com.distributed.task.common.result.ResultCode;
import com.distributed.task.common.utils.TaskNoGenerator;
import com.distributed.task.scheduler.mapper.TaskInfoMapper;
import com.distributed.task.scheduler.service.RetryStrategyService;
import com.distributed.task.scheduler.service.TaskService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;
import java.util.UUID;

@Slf4j
@Service
@RequiredArgsConstructor
public class TaskServiceImpl implements TaskService {

    private final TaskInfoMapper taskInfoMapper;
    private final IdempotentClient idempotentClient;
    private final AlarmClient alarmClient;
    private final StringRedisTemplate redisTemplate;
    private final RetryStrategyService retryStrategyService;

    @Value("${task.scheduler.node-id}")
    private String nodeId;
    @Value("${task.scheduler.max-retry-count:5}")
    private Integer defaultMaxRetryCount;
    @Value("${task.scheduler.default-timeout-seconds:300}")
    private Integer defaultTimeoutSeconds;
    @Value("${task.retry.ready-queue-prefix:task:retry:ready:}")
    private String readyQueuePrefix;

    private static final String QUEUED_SET_KEY = "task:retry:queued:set";
    private static final String QUEUE_INDEX_KEY = "task:retry:queue:index";

    @Override
    @Transactional(rollbackFor = Exception.class)
    public String submit(TaskSubmitDTO dto) {
        IdempotentDTO idem = new IdempotentDTO();
        idem.setTaskType(dto.getTaskType());
        idem.setBizKey(dto.getBizKey());
        idem.setExpireSeconds(dto.getIdempotentExpireSeconds());
        R<Boolean> idemRes;
        try {
            idemRes = idempotentClient.check(idem);
        } catch (Exception e) {
            log.error("幂等校验Feign调用失败 taskType={} bizKey={}", dto.getTaskType(), dto.getBizKey(), e);
            throw new IllegalStateException("幂等校验服务不可用，请稍后重试");
        }
        if (idemRes == null || idemRes.getCode() != 200 || !Boolean.TRUE.equals(idemRes.getData())) {
            throw new IllegalStateException(ResultCode.IDEMPOTENT_DUPLICATE.getMsg());
        }

        RetryStrategy strategy = retryStrategyService.getByTaskType(dto.getTaskType());
        int maxRetry = strategy != null && strategy.getMaxRetryCount() != null
                ? strategy.getMaxRetryCount()
                : (dto.getMaxRetryCount() != null ? dto.getMaxRetryCount() : defaultMaxRetryCount);
        int timeout = strategy != null && strategy.getTimeoutSeconds() != null
                ? strategy.getTimeoutSeconds()
                : (dto.getTimeoutSeconds() != null ? dto.getTimeoutSeconds() : defaultTimeoutSeconds);

        TaskInfo info = new TaskInfo();
        info.setTaskNo(TaskNoGenerator.generate());
        info.setTaskType(dto.getTaskType());
        info.setBizKey(dto.getBizKey());
        info.setTaskPayload(dto.getTaskPayload());
        info.setCallbackUrl(dto.getCallbackUrl());
        info.setStatus(TaskStatus.PENDING.getCode());
        info.setRetryCount(0);
        info.setMaxRetryCount(maxRetry);
        info.setRetryLevel(RetryLevel.LEVEL_1.getLevel());
        info.setTimeoutSeconds(timeout);
        info.setPriority(dto.getPriority() != null ? dto.getPriority() : TaskPriority.NORMAL.getLevel());
        info.setTraceId(UUID.randomUUID().toString().replace("-", ""));
        info.setOwnerNode(nodeId);
        info.setFirstExecuteTime(LocalDateTime.now());

        try {
            taskInfoMapper.insert(info);
        } catch (DuplicateKeyException e) {
            log.warn("DB唯一索引命中，任务已存在 taskType={} bizKey={}", dto.getTaskType(), dto.getBizKey());
            releaseIdempotent(dto);
            throw new IllegalStateException(ResultCode.TASK_ALREADY_EXISTS.getMsg());
        } catch (Exception e) {
            log.error("任务入库失败 taskType={} bizKey={}", dto.getTaskType(), dto.getBizKey(), e);
            releaseIdempotent(dto);
            throw new RuntimeException("任务入库失败：" + e.getMessage(), e);
        }

        enqueueTask(info);
        return info.getTaskNo();
    }

    private void enqueueTask(TaskInfo info) {
        String queueKey = readyQueuePrefix + info.getTaskType();
        String json = JSON.toJSONString(info);
        long ts = System.currentTimeMillis();
        double score = TaskPriority.of(info.getPriority()).toScore(ts);
        try {
            redisTemplate.opsForZSet().add(queueKey, json, score);
            redisTemplate.opsForSet().add(QUEUE_INDEX_KEY, queueKey);
        } catch (Exception e) {
            log.error("任务入队Redis失败 taskNo={}", info.getTaskNo(), e);
            return;
        }
        try {
            redisTemplate.opsForSet().add(QUEUED_SET_KEY, info.getTaskNo());
        } catch (Exception e) {
            log.warn("入队集合写入失败 taskNo={}", info.getTaskNo(), e);
        }
        try {
            LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
            uw.eq(TaskInfo::getId, info.getId())
              .in(TaskInfo::getStatus, TaskStatus.PENDING.getCode(), TaskStatus.RETRY_WAIT.getCode())
              .set(TaskInfo::getStatus, TaskStatus.QUEUED.getCode());
            int rows = taskInfoMapper.update(null, uw);
            if (rows <= 0) {
                log.warn("入队后状态更新失败，任务可能已被领取 taskNo={}", info.getTaskNo());
                redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());
            } else {
                log.info("任务已入队 taskNo={} priority={}", info.getTaskNo(), info.getPriority());
            }
        } catch (Exception e) {
            log.warn("入队后状态更新异常 taskNo={}", info.getTaskNo(), e);
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());
        }
    }

    private void releaseIdempotent(TaskSubmitDTO dto) {
        try {
            IdempotentDTO idem = new IdempotentDTO();
            idem.setTaskType(dto.getTaskType());
            idem.setBizKey(dto.getBizKey());
            idempotentClient.release(idem);
        } catch (Exception ex) {
            log.warn("幂等键释放失败 taskType={} bizKey={}", dto.getTaskType(), dto.getBizKey(), ex);
        }
    }

    @Override
    public TaskVO getByTaskNo(String taskNo) {
        TaskInfo info = taskInfoMapper.selectOne(new LambdaQueryWrapper<TaskInfo>().eq(TaskInfo::getTaskNo, taskNo));
        return toVO(info);
    }

    @Override
    public Page<TaskVO> page(int current, int size, String taskType, Integer status) {
        LambdaQueryWrapper<TaskInfo> qw = new LambdaQueryWrapper<>();
        if (StrUtil.isNotBlank(taskType)) {
            qw.eq(TaskInfo::getTaskType, taskType);
        }
        if (status != null) {
            qw.eq(TaskInfo::getStatus, status);
        }
        qw.orderByAsc(TaskInfo::getPriority).orderByDesc(TaskInfo::getId);
        Page<TaskInfo> page = taskInfoMapper.selectPage(new Page<>(current, size), qw);
        Page<TaskVO> voPage = new Page<>(page.getCurrent(), page.getSize(), page.getTotal());
        voPage.setRecords(BeanUtil.copyToList(page.getRecords(), TaskVO.class));
        voPage.getRecords().forEach(v -> {
            TaskStatus ts = TaskStatus.of(v.getStatus());
            v.setStatusDesc(ts != null ? ts.getDesc() : "");
        });
        return voPage;
    }

    @Override
    public boolean cancel(String taskNo) {
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getTaskNo, taskNo)
          .in(TaskInfo::getStatus,
              TaskStatus.PENDING.getCode(),
              TaskStatus.RETRY_WAIT.getCode(),
              TaskStatus.QUEUED.getCode())
          .set(TaskInfo::getStatus, TaskStatus.CANCELLED.getCode());
        int rows = taskInfoMapper.update(null, uw);
        if (rows > 0) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, taskNo);
        }
        return rows > 0;
    }

    @Override
    public boolean handleExecuteResult(TaskExecuteDTO dto) {
        TaskInfo info = taskInfoMapper.selectOne(new LambdaQueryWrapper<TaskInfo>().eq(TaskInfo::getTaskNo, dto.getTaskNo()));
        if (info == null) {
            log.warn("上报结果但任务不存在 taskNo={}", dto.getTaskNo());
            return false;
        }
        if (Integer.valueOf(1).equals(dto.getSuccess())) {
            return markSuccess(info.getId(), dto.getResult());
        }
        return markFailed(info.getId(), dto.getErrorMessage());
    }

    @Override
    public boolean markRunning(Long taskId, String ownerNode) {
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getId, taskId)
          .in(TaskInfo::getStatus,
              TaskStatus.PENDING.getCode(),
              TaskStatus.RETRY_WAIT.getCode(),
              TaskStatus.QUEUED.getCode())
          .set(TaskInfo::getStatus, TaskStatus.RUNNING.getCode())
          .set(TaskInfo::getLastExecuteTime, LocalDateTime.now())
          .set(TaskInfo::getOwnerNode, ownerNode);
        int rows = taskInfoMapper.update(null, uw);
        if (rows > 0) {
            TaskInfo info = taskInfoMapper.selectById(taskId);
            if (info != null) {
                redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());
            }
        }
        return rows > 0;
    }

    @Override
    public boolean markSuccess(Long taskId, String result) {
        TaskInfo info = taskInfoMapper.selectById(taskId);
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getId, taskId)
          .set(TaskInfo::getStatus, TaskStatus.SUCCESS.getCode())
          .set(TaskInfo::getRemark, StrUtil.maxLength(result, 500));
        int rows = taskInfoMapper.update(null, uw);
        if (rows > 0 && info != null) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());
        }
        return rows > 0;
    }

    @Override
    public boolean markFailed(Long taskId, String errorMessage) {
        TaskInfo info = taskInfoMapper.selectById(taskId);
        if (info == null) {
            return false;
        }
        redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());

        int nextRetryCount = info.getRetryCount() + 1;
        if (nextRetryCount >= info.getMaxRetryCount()) {
            LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
            uw.eq(TaskInfo::getId, taskId)
              .set(TaskInfo::getStatus, TaskStatus.FAILED.getCode())
              .set(TaskInfo::getRetryCount, nextRetryCount)
              .set(TaskInfo::getRemark, StrUtil.maxLength(errorMessage, 500));
            taskInfoMapper.update(null, uw);
            sendAlarm(info.getTaskNo(), "TASK_FAIL", 5, "任务最终失败：" + errorMessage);
            return true;
        }
        RetryLevel nextLevel = RetryLevel.next(info.getRetryLevel() != null ? info.getRetryLevel() : 1);
        LocalDateTime nextRetry = LocalDateTime.now().plusSeconds(nextLevel.getIntervalSeconds());
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getId, taskId)
          .set(TaskInfo::getStatus, TaskStatus.RETRY_WAIT.getCode())
          .set(TaskInfo::getRetryCount, nextRetryCount)
          .set(TaskInfo::getRetryLevel, nextLevel.getLevel())
          .set(TaskInfo::getNextRetryTime, nextRetry)
          .set(TaskInfo::getRemark, StrUtil.maxLength(errorMessage, 500));
        taskInfoMapper.update(null, uw);
        sendAlarm(info.getTaskNo(), "TASK_RETRY", nextLevel.getLevel(),
                "任务失败，将在" + nextLevel.getIntervalSeconds() + "秒后重试：" + errorMessage);
        return true;
    }

    @Override
    public boolean markTimeout(Long taskId) {
        TaskInfo info = taskInfoMapper.selectById(taskId);
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getId, taskId)
          .set(TaskInfo::getStatus, TaskStatus.TIMEOUT.getCode());
        int rows = taskInfoMapper.update(null, uw);
        if (rows > 0 && info != null) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, info.getTaskNo());
            sendAlarm(info.getTaskNo(), "TASK_TIMEOUT", 4, "任务执行超时");
        }
        return rows > 0;
    }

    private void sendAlarm(String taskNo, String type, Integer level, String content) {
        try {
            AlarmDTO alarm = new AlarmDTO();
            alarm.setTaskNo(taskNo);
            alarm.setAlarmType(type);
            alarm.setAlarmLevel(level);
            alarm.setAlarmContent(content);
            alarmClient.send(alarm);
        } catch (Exception e) {
            log.warn("发送告警失败 taskNo={} err={}", taskNo, e.getMessage());
        }
    }

    private TaskVO toVO(TaskInfo info) {
        if (info == null) {
            return null;
        }
        TaskVO vo = BeanUtil.copyProperties(info, TaskVO.class);
        TaskStatus ts = TaskStatus.of(info.getStatus());
        vo.setStatusDesc(ts != null ? ts.getDesc() : "");
        return vo;
    }
}
