package com.distributed.task.common.feign;

import com.distributed.task.common.dto.AlarmDTO;
import com.distributed.task.common.result.R;
import org.springframework.cloud.openfeign.FeignClient;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;

@FeignClient(name = "task-alarm", contextId = "alarmClient")
public interface AlarmClient {

    @PostMapping("/send")
    R<Boolean> send(@RequestBody AlarmDTO dto);
}
