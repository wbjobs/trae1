package com.cdn.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.domain.dto.BatchRefreshDTO;
import com.cdn.domain.entity.AlertRecord;
import com.cdn.domain.entity.RefreshLog;
import com.cdn.domain.mapper.AlertRecordMapper;
import com.cdn.domain.mapper.RefreshLogMapper;
import com.cdn.service.cache.CacheService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Async;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;

@Slf4j
@Service
@RequiredArgsConstructor
public class RefreshService {

    private final RefreshLogMapper logMapper;
    private final AlertRecordMapper alertMapper;
    private final CacheService cacheService;

    private final Map<String, CompletableFuture<Map<String, Object>>> taskMap = new ConcurrentHashMap<>();

    public CompletableFuture<Map<String, Object>> batchRefresh(BatchRefreshDTO dto) {
        String taskId = UUID.randomUUID().toString().replace("-", "").substring(0, 16);
        String operator = dto.getOperator() == null ? "system" : dto.getOperator();

        CompletableFuture<Map<String, Object>> future = new CompletableFuture<>();
        taskMap.put(taskId, future);

        cacheService.asyncRefreshBatch(dto.getResourceUrls(), operator, result -> {
            List<String> failUrls = (List<String>) result.get("failUrls");
            saveRefreshLogs(dto.getResourceUrls(), dto.getRefreshType(), operator,
                    (int) result.get("success"), (int) result.get("fail"),
                    failUrls, (Long) result.get("costTime"));

            if (!failUrls.isEmpty()) {
                saveAlert(failUrls, (int) result.get("fail"));
            }

            result.put("taskId", taskId);
            future.complete(result);
            taskMap.remove(taskId);
        });

        return future;
    }

    public Map<String, Object> getTaskStatus(String taskId) {
        CompletableFuture<Map<String, Object>> f = taskMap.get(taskId);
        Map<String, Object> m = new HashMap<>();
        m.put("taskId", taskId);
        if (f == null) {
            m.put("status", "not_found");
        } else if (f.isDone()) {
            m.put("status", "completed");
            try {
                m.putAll(f.get());
            } catch (Exception e) {
                m.put("error", e.getMessage());
            }
        } else {
            m.put("status", "running");
        }
        return m;
    }

    private void saveRefreshLogs(List<String> urls, Integer type, String operator,
                                  int success, int fail, List<String> failUrls, long cost) {
        LocalDateTime now = LocalDateTime.now();
        for (String url : urls) {
            RefreshLog log = new RefreshLog();
            log.setResourceUrl(url);
            log.setRefreshType(type == 1 ? "DELETE" : "UPDATE");
            log.setOperator(operator);
            log.setCostTime(cost / urls.size());
            log.setCreateTime(now);
            if (failUrls.contains(url)) {
                log.setRefreshStatus(0);
                log.setFailReason("refresh_failed");
            } else {
                log.setRefreshStatus(1);
            }
            logMapper.insert(log);
        }
    }

    private void saveAlert(List<String> failUrls, int failCount) {
        AlertRecord alert = new AlertRecord();
        alert.setResourceUrl(String.join(",", failUrls.subList(0, Math.min(failUrls.size(), 5))));
        alert.setAlertType("REFRESH_FAIL");
        alert.setAlertContent("批量刷新失败" + failCount + "个资源");
        alert.setAlertLevel(failCount > 10 ? 2 : 1);
        alert.setHandled(0);
        alert.setCreateTime(LocalDateTime.now());
        alertMapper.insert(alert);
    }

    public Page<RefreshLog> logPage(int pageNum, int pageSize, String keyword) {
        Page<RefreshLog> page = new Page<>(pageNum, pageSize);
        LambdaQueryWrapper<RefreshLog> qw = new LambdaQueryWrapper<>();
        if (keyword != null && !keyword.isEmpty()) {
            qw.like(RefreshLog::getResourceUrl, keyword)
                    .or().like(RefreshLog::getOperator, keyword);
        }
        qw.orderByDesc(RefreshLog::getCreateTime);
        return logMapper.selectPage(page, qw);
    }

    public Page<AlertRecord> alertPage(int pageNum, int pageSize) {
        Page<AlertRecord> page = new Page<>(pageNum, pageSize);
        LambdaQueryWrapper<AlertRecord> qw = new LambdaQueryWrapper<>();
        qw.orderByDesc(AlertRecord::getCreateTime);
        return alertMapper.selectPage(page, qw);
    }

    public void handleAlert(Long id, String result) {
        AlertRecord a = alertMapper.selectById(id);
        if (a != null) {
            a.setHandled(1);
            a.setHandleResult(result);
            a.setHandleTime(LocalDateTime.now());
            alertMapper.updateById(a);
        }
    }
}
