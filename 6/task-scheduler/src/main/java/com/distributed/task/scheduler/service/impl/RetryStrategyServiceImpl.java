package com.distributed.task.scheduler.service.impl;

import cn.hutool.core.util.StrUtil;
import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.distributed.task.common.entity.RetryStrategy;
import com.distributed.task.scheduler.mapper.RetryStrategyMapper;
import com.distributed.task.scheduler.service.RetryStrategyService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

import javax.annotation.PostConstruct;
import javax.annotation.Resource;
import java.time.LocalDateTime;
import java.util.List;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Service
public class RetryStrategyServiceImpl implements RetryStrategyService {

    @Resource
    private RetryStrategyMapper retryStrategyMapper;

    private final ConcurrentHashMap<String, RetryStrategy> cache = new ConcurrentHashMap<>();

    @PostConstruct
    public void init() {
        refreshCache();
    }

    @Override
    public RetryStrategy getByTaskType(String taskType) {
        if (StrUtil.isBlank(taskType)) {
            return null;
        }
        RetryStrategy s = cache.get(taskType);
        if (s == null || Integer.valueOf(0).equals(s.getEnabled())) {
            return null;
        }
        return s;
    }

    @Override
    public Page<RetryStrategy> page(int current, int size, String taskType) {
        LambdaQueryWrapper<RetryStrategy> qw = new LambdaQueryWrapper<>();
        if (StrUtil.isNotBlank(taskType)) {
            qw.eq(RetryStrategy::getTaskType, taskType);
        }
        qw.orderByDesc(RetryStrategy::getId);
        return retryStrategyMapper.selectPage(new Page<>(current, size), qw);
    }

    @Override
    public RetryStrategy save(RetryStrategy strategy) {
        strategy.setUpdateTime(LocalDateTime.now());
        if (strategy.getId() == null) {
            strategy.setCreateTime(LocalDateTime.now());
            if (strategy.getEnabled() == null) {
                strategy.setEnabled(1);
            }
            retryStrategyMapper.insert(strategy);
        } else {
            retryStrategyMapper.updateById(strategy);
        }
        refreshCache();
        return strategy;
    }

    @Override
    public boolean delete(Long id) {
        int rows = retryStrategyMapper.deleteById(id);
        if (rows > 0) {
            refreshCache();
        }
        return rows > 0;
    }

    @Override
    @Scheduled(fixedDelay = 60_000)
    public synchronized void refreshCache() {
        try {
            List<RetryStrategy> all = retryStrategyMapper.selectList(null);
            cache.clear();
            for (RetryStrategy s : all) {
                cache.put(s.getTaskType(), s);
            }
            log.info("重试策略缓存已刷新，共{}条", cache.size());
        } catch (Exception e) {
            log.error("重试策略缓存刷新失败", e);
        }
    }
}
