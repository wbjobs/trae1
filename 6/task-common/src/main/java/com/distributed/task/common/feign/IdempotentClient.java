package com.distributed.task.common.feign;

import com.distributed.task.common.dto.IdempotentDTO;
import com.distributed.task.common.result.R;
import org.springframework.cloud.openfeign.FeignClient;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;

@FeignClient(name = "task-idempotent", contextId = "idempotentClient")
public interface IdempotentClient {

    @PostMapping("/idempotent/check")
    R<Boolean> check(@RequestBody IdempotentDTO dto);

    @PostMapping("/idempotent/release")
    R<Boolean> release(@RequestBody IdempotentDTO dto);
}
