package com.distributed.task.common.feign;

import com.distributed.task.common.entity.TaskLog;
import com.distributed.task.common.result.R;
import org.springframework.cloud.openfeign.FeignClient;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;

import java.util.List;

@FeignClient(name = "task-log", contextId = "taskLogQueryClient")
public interface TaskLogQueryClient {

    @GetMapping("/log/list-by-task")
    R<List<TaskLog>> listByTaskNo(@RequestParam("taskNo") String taskNo);
}
