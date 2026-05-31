package com.distributed.task.scheduler.job;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.core.conditions.update.LambdaUpdateWrapper;
import com.distributed.task.common.entity.TaskInfo;
import com.distributed.task.common.enums.TaskStatus;
import com.distributed.task.common.lock.DistributedLockHelper;
import com.distributed.task.scheduler.mapper.TaskInfoMapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.time.LocalDateTime;
import java.util.List;

@Slf4j
@Component
@RequiredArgsConstructor
public class TaskTimeoutJob {

    private final TaskInfoMapper taskInfoMapper;
    private final StringRedisTemplate redisTemplate;

    private static final String LOCK_KEY = "task:scheduler:timeout:lock";
    private static final long LOCK_LEASE_MS = 30_000L;

    @Scheduled(fixedDelay = 30_000)
    public void scanAndMarkTimeout() {
        DistributedLockHelper lock = DistributedLockHelper.newLock(redisTemplate, LOCK_KEY, LOCK_LEASE_MS);
        if (!lock.tryLock(500)) {
            return;
        }
        try {
            LambdaQueryWrapper<TaskInfo> qw = new LambdaQueryWrapper<>();
            qw.eq(TaskInfo::getStatus, TaskStatus.RUNNING.getCode());
            List<TaskInfo> running = taskInfoMapper.selectList(qw);
            LocalDateTime now = LocalDateTime.now();
            for (TaskInfo t : running) {
                if (t.getLastExecuteTime() == null || t.getTimeoutSeconds() == null) {
                    continue;
                }
                if (t.getLastExecuteTime().plusSeconds(t.getTimeoutSeconds()).isBefore(now)) {
                    LambdaUpdateWrapper<TaskInfo> uw = new LambdaUpdateWrapper<>();
                    uw.eq(TaskInfo::getId, t.getId())
                      .eq(TaskInfo::getStatus, TaskStatus.RUNNING.getCode())
                      .set(TaskInfo::getStatus, TaskStatus.TIMEOUT.getCode())
                      .set(TaskInfo::getRemark, "任务执行超时");
                    taskInfoMapper.update(null, uw);
                    log.warn("任务超时 taskNo={}", t.getTaskNo());
                }
            }
        } catch (Exception e) {
            log.error("超时扫描异常", e);
        } finally {
            lock.unlock();
        }
    }
}
