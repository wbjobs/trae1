package com.sharding.sync.sync.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.impl.SyncTaskServiceImpl;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

@Slf4j
@Service
@RequiredArgsConstructor
public class SyncLogService {

    private final SyncTaskServiceImpl syncTaskService;

    public List<Map<String, Object>> exportLogs(String logicTable, String syncType, String status,
                                                LocalDateTime startTime, LocalDateTime endTime,
                                                Integer limit) {
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        if (logicTable != null && !logicTable.isEmpty()) {
            wrapper.eq(SyncTask::getLogicTable, logicTable);
        }
        if (syncType != null && !syncType.isEmpty()) {
            wrapper.eq(SyncTask::getSyncType, syncType);
        }
        if (status != null && !status.isEmpty()) {
            wrapper.eq(SyncTask::getStatus, status);
        }
        if (startTime != null) {
            wrapper.ge(SyncTask::getCreateTime, startTime);
        }
        if (endTime != null) {
            wrapper.le(SyncTask::getCreateTime, endTime);
        }
        wrapper.orderByDesc(SyncTask::getCreateTime);
        if (limit != null && limit > 0) {
            wrapper.last("limit " + limit);
        } else {
            wrapper.last("limit 1000");
        }
        List<SyncTask> tasks = syncTaskService.list(wrapper);
        List<Map<String, Object>> result = new ArrayList<>();
        DateTimeFormatter fmt = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
        for (SyncTask task : tasks) {
            Map<String, Object> row = new LinkedHashMap<>();
            row.put("taskNo", task.getTaskNo());
            row.put("taskName", task.getTaskName());
            row.put("syncType", task.getSyncType());
            row.put("logicTable", task.getLogicTable());
            row.put("status", task.getStatus());
            row.put("triggerMode", task.getTriggerMode());
            row.put("totalCount", task.getTotalCount());
            row.put("successCount", task.getSuccessCount());
            row.put("failCount", task.getFailCount());
            row.put("startTime", task.getStartTime() != null ? task.getStartTime().format(fmt) : "");
            row.put("endTime", task.getEndTime() != null ? task.getEndTime().format(fmt) : "");
            row.put("errorMsg", task.getErrorMsg() != null ? task.getErrorMsg() : "");
            row.put("retryOf", task.getRetryOf());
            row.put("createTime", task.getCreateTime() != null ? task.getCreateTime().format(fmt) : "");
            result.add(row);
        }
        return result;
    }

    public String exportCsv(String logicTable, String syncType, String status,
                            LocalDateTime startTime, LocalDateTime endTime, Integer limit) {
        List<Map<String, Object>> logs = exportLogs(logicTable, syncType, status, startTime, endTime, limit);
        StringBuilder sb = new StringBuilder();
        sb.append("taskNo,taskName,syncType,logicTable,status,triggerMode,totalCount,successCount,failCount,startTime,endTime,errorMsg,retryOf,createTime\n");
        for (Map<String, Object> row : logs) {
            sb.append(escapeCsv(row.get("taskNo"))).append(",");
            sb.append(escapeCsv(row.get("taskName"))).append(",");
            sb.append(escapeCsv(row.get("syncType"))).append(",");
            sb.append(escapeCsv(row.get("logicTable"))).append(",");
            sb.append(escapeCsv(row.get("status"))).append(",");
            sb.append(escapeCsv(row.get("triggerMode"))).append(",");
            sb.append(escapeCsv(row.get("totalCount"))).append(",");
            sb.append(escapeCsv(row.get("successCount"))).append(",");
            sb.append(escapeCsv(row.get("failCount"))).append(",");
            sb.append(escapeCsv(row.get("startTime"))).append(",");
            sb.append(escapeCsv(row.get("endTime"))).append(",");
            sb.append(escapeCsv(row.get("errorMsg"))).append(",");
            sb.append(escapeCsv(row.get("retryOf"))).append(",");
            sb.append(escapeCsv(row.get("createTime"))).append("\n");
        }
        return sb.toString();
    }

    private String escapeCsv(Object value) {
        if (value == null) return "";
        String s = String.valueOf(value);
        if (s.contains(",") || s.contains("\"") || s.contains("\n")) {
            return "\"" + s.replace("\"", "\"\"") + "\"";
        }
        return s;
    }

    public Map<String, Object> getSyncStatistics(String logicTable, LocalDateTime startTime, LocalDateTime endTime) {
        LambdaQueryWrapper<SyncTask> wrapper = new LambdaQueryWrapper<>();
        if (logicTable != null && !logicTable.isEmpty()) {
            wrapper.eq(SyncTask::getLogicTable, logicTable);
        }
        if (startTime != null) {
            wrapper.ge(SyncTask::getCreateTime, startTime);
        }
        if (endTime != null) {
            wrapper.le(SyncTask::getCreateTime, endTime);
        }
        List<SyncTask> tasks = syncTaskService.list(wrapper);
        Map<String, Object> stats = new LinkedHashMap<>();
        stats.put("totalTasks", tasks.size());
        long success = 0, partial = 0, failed = 0, canceled = 0;
        long totalRecords = 0, successRecords = 0, failRecords = 0;
        Map<String, Long> byType = new LinkedHashMap<>();
        for (SyncTask task : tasks) {
            String st = task.getStatus();
            if ("SUCCESS".equals(st)) success++;
            else if ("PARTIAL".equals(st)) partial++;
            else if ("FAILED".equals(st)) failed++;
            else if ("CANCELED".equals(st)) canceled++;
            totalRecords += task.getTotalCount() != null ? task.getTotalCount() : 0;
            successRecords += task.getSuccessCount() != null ? task.getSuccessCount() : 0;
            failRecords += task.getFailCount() != null ? task.getFailCount() : 0;
            byType.merge(task.getSyncType(), 1L, Long::sum);
        }
        stats.put("successTasks", success);
        stats.put("partialTasks", partial);
        stats.put("failedTasks", failed);
        stats.put("canceledTasks", canceled);
        stats.put("totalRecords", totalRecords);
        stats.put("successRecords", successRecords);
        stats.put("failRecords", failRecords);
        stats.put("byType", byType);
        double successRate = totalRecords > 0 ? (successRecords * 100.0 / totalRecords) : 0;
        stats.put("successRate", String.format("%.2f%%", successRate));
        return stats;
    }
}
