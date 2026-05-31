package com.distributed.task.common.feign;

import com.distributed.task.common.dto.TaskExecuteDTO;
import com.distributed.task.common.result.R;
import org.springframework.cloud.openfeign.FeignClient;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;

import java.util.Map;

@FeignClient(name = "task-log", contextId = "taskLogClient")
public interface TaskLogClient {

    @PostMapping("/log/record")
    R<Map<String, Long>> record(@RequestBody TaskExecuteDTO dto, @RequestParam(value = "retryNo", required = false) Integer retryNo);
}
