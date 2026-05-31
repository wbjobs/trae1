package com.distributed.task.idempotent.service;

import com.distributed.task.common.dto.IdempotentDTO;

public interface IdempotentService {

    boolean check(IdempotentDTO dto);

    boolean release(IdempotentDTO dto);
}
