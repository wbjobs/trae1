package com.distributed.task.common.feign;

import com.distributed.task.common.entity.AlarmRecord;
import com.distributed.task.common.result.R;
import org.springframework.cloud.openfeign.FeignClient;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;

import java.util.List;

@FeignClient(name = "task-alarm", contextId = "alarmQueryClient")
public interface AlarmQueryClient {

    @GetMapping("/alarm/list-by-task")
    R<List<AlarmRecord>> listByTaskNo(@RequestParam("taskNo") String taskNo);
}
