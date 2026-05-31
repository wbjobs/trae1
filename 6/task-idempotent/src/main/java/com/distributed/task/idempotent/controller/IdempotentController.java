package com.distributed.task.idempotent.controller;

import com.distributed.task.common.dto.IdempotentDTO;
import com.distributed.task.common.result.R;
import com.distributed.task.idempotent.service.IdempotentService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

@Api(tags = "幂等校验")
@RestController
@RequestMapping("/idempotent")
@RequiredArgsConstructor
public class IdempotentController {

    private final IdempotentService idempotentService;

    @ApiOperation("幂等校验")
    @PostMapping("/check")
    public R<Boolean> check(@RequestBody IdempotentDTO dto) {
        return R.ok(idempotentService.check(dto));
    }

    @ApiOperation("释放幂等键")
    @PostMapping("/release")
    public R<Boolean> release(@RequestBody IdempotentDTO dto) {
        return R.ok(idempotentService.release(dto));
    }
}
