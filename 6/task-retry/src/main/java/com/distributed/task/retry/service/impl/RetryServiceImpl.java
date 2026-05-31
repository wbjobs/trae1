package com.distributed.task.retry.service.impl;

import cn.hutool.core.bean.BeanUtil;
import com.alibaba.fastjson2.JSON;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.conditions.update.LambdaUpdateWrapper;
import com.distributed.task.common.dto.TaskVO;
import com.distributed.task.common.entity.TaskInfo;
import com.distributed.task.common.enums.TaskPriority;
import com.distributed.task.common.enums.TaskStatus;
import com.distributed.task.retry.mapper.RetryTaskMapper;
import com.distributed.task.retry.service.RetryService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;

@Slf4j
@Service
@RequiredArgsConstructor
public class RetryServiceImpl implements RetryService {

    private final RetryTaskMapper retryTaskMapper;
    private final StringRedisTemplate redisTemplate;

    @Value("${task.retry.batch-size:50}")
    private int batchSize;

    @Value("${task.retry.ready-queue-prefix:task:retry:ready:}")
    private String readyQueuePrefix;

    private static final String QUEUED_SET_KEY = "task:retry:queued:set";
    private static final String QUEUE_INDEX_KEY = "task:retry:queue:index";

    @Override
    public List<TaskVO> scanDueTasks() {
        List<TaskInfo> due = retryTaskMapper.selectDueTasks(LocalDateTime.now(), batchSize);
        if (due == null || due.isEmpty()) {
            return Collections.emptyList();
        }
        List<TaskVO> result = new ArrayList<>();
        for (TaskInfo t : due) {
            Boolean isNew = redisTemplate.opsForSet().add(QUEUED_SET_KEY, t.getTaskNo());
            if (Boolean.FALSE.equals(isNew)) {
                continue;
            }
            String queueKey = readyQueuePrefix + t.getTaskType();
            String json = JSON.toJSONString(t);
            long ts = System.currentTimeMillis();
            double score = TaskPriority.of(t.getPriority()).toScore(ts);
            try {
                redisTemplate.opsForZSet().add(queueKey, json, score);
                redisTemplate.opsForSet().add(QUEUE_INDEX_KEY, queueKey);
            } catch (Exception e) {
                redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
                log.error("任务入队Redis失败 taskNo={}", t.getTaskNo(), e);
                continue;
            }
            int updated = retryTaskMapper.markQueued(t.getId());
            if (updated <= 0) {
                redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
                redisTemplate.opsForZSet().remove(queueKey, json);
                log.debug("任务状态不允许入队 taskNo={} currentStatus={}", t.getTaskNo(), t.getStatus());
                continue;
            }
            log.info("任务入队 taskNo={} priority={}", t.getTaskNo(), t.getPriority());
            result.add(toVO(t));
        }
        return result;
    }

    @Override
    public boolean triggerRetry(String taskNo) {
        TaskInfo t = retryTaskMapper.selectOne(new LambdaQueryWrapper<TaskInfo>().eq(TaskInfo::getTaskNo, taskNo));
        if (t == null) {
            return false;
        }
        if (t.getStatus() != TaskStatus.FAILED.getCode()
                && t.getStatus() != TaskStatus.TIMEOUT.getCode()
                && t.getStatus() != TaskStatus.RETRY_WAIT.getCode()
                && t.getStatus() != TaskStatus.PENDING.getCode()) {
            return false;
        }
        Boolean isNew = redisTemplate.opsForSet().add(QUEUED_SET_KEY, t.getTaskNo());
        if (Boolean.FALSE.equals(isNew)) {
            return false;
        }
        String queueKey = readyQueuePrefix + t.getTaskType();
        String json = JSON.toJSONString(t);
        long ts = System.currentTimeMillis();
        double score = TaskPriority.of(t.getPriority()).toScore(ts);
        try {
            redisTemplate.opsForZSet().add(queueKey, json, score);
            redisTemplate.opsForSet().add(QUEUE_INDEX_KEY, queueKey);
        } catch (Exception e) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
            log.error("手动触发入队失败 taskNo={}", t.getTaskNo(), e);
            return false;
        }
        int updated = retryTaskMapper.markQueued(t.getId());
        if (updated <= 0) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
            redisTemplate.opsForZSet().remove(queueKey, json);
            return false;
        }
        return true;
    }

    @Override
    public boolean claimAndDispatch(String taskNo, String workerNode) {
        TaskInfo t = retryTaskMapper.selectOne(new LambdaQueryWrapper<TaskInfo>().eq(TaskInfo::getTaskNo, taskNo));
        if (t == null) {
            return false;
        }
        LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
        uw.eq(TaskInfo::getId, t.getId())
          .in(TaskInfo::getStatus,
              TaskStatus.PENDING.getCode(),
              TaskStatus.RETRY_WAIT.getCode(),
              TaskStatus.QUEUED.getCode())
          .set(TaskInfo::getStatus, TaskStatus.RUNNING.getCode())
          .set(TaskInfo::getLastExecuteTime, LocalDateTime.now())
          .set(TaskInfo::getOwnerNode, workerNode);
        int rows = retryTaskMapper.update(null, uw);
        if (rows > 0) {
            redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
        }
        return rows > 0;
    }

    @Override
    public String claim(String workerNode) {
        Set<String> queueKeys = redisTemplate.opsForSet().members(QUEUE_INDEX_KEY);
        if (queueKeys == null || queueKeys.isEmpty()) {
            return null;
        }
        for (String key : queueKeys) {
            Long zSize = redisTemplate.opsForZSet().size(key);
            if (zSize == null || zSize == 0) {
                redisTemplate.opsForSet().remove(QUEUE_INDEX_KEY, key);
                continue;
            }
            var tupleSet = redisTemplate.opsForZSet().popMin(key, 1);
            if (tupleSet == null || tupleSet.isEmpty()) {
                continue;
            }
            String json = tupleSet.iterator().next().getValue();
            TaskInfo t;
            try {
                t = JSON.parseObject(json, TaskInfo.class);
            } catch (Exception e) {
                log.warn("队列消息解析失败 key={} json={}", key, json);
                continue;
            }
            LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
            uw.eq(TaskInfo::getId, t.getId())
              .in(TaskInfo::getStatus,
                  TaskStatus.PENDING.getCode(),
                  TaskStatus.RETRY_WAIT.getCode(),
                  TaskStatus.QUEUED.getCode())
              .set(TaskInfo::getStatus, TaskStatus.RUNNING.getCode())
              .set(TaskInfo::getLastExecuteTime, LocalDateTime.now())
              .set(TaskInfo::getOwnerNode, workerNode);
            int rows = retryTaskMapper.update(null, uw);
            if (rows > 0) {
                redisTemplate.opsForSet().remove(QUEUED_SET_KEY, t.getTaskNo());
                log.info("领取任务 taskNo={} priority={} worker={}", t.getTaskNo(), t.getPriority(), workerNode);
                return t.getTaskNo();
            } else {
                log.warn("领取失败，状态已变更 taskNo={} dbStatus={}", t.getTaskNo(), t.getStatus());
            }
        }
        return null;
    }

    private TaskVO toVO(TaskInfo t) {
        TaskVO vo = BeanUtil.copyProperties(t, TaskVO.class);
        TaskStatus ts = TaskStatus.of(t.getStatus());
        vo.setStatusDesc(ts != null ? ts.getDesc() : "");
        return vo;
    }
}
