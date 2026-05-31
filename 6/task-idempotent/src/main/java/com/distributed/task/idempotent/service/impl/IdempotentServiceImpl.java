package com.distributed.task.idempotent.service.impl;

import cn.hutool.core.util.StrUtil;
import com.distributed.task.common.dto.IdempotentDTO;
import com.distributed.task.idempotent.service.IdempotentService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;

import java.util.concurrent.TimeUnit;

@Slf4j
@Service
@RequiredArgsConstructor
public class IdempotentServiceImpl implements IdempotentService {

    private final StringRedisTemplate redisTemplate;

    @Value("${task.idempotent.key-prefix:task:idempotent}")
    private String keyPrefix;

    @Value("${task.idempotent.default-expire-seconds:86400}")
    private Integer defaultExpireSeconds;

    @Override
    public boolean check(IdempotentDTO dto) {
        String key = buildKey(dto);
        Integer expire = dto.getExpireSeconds() != null ? dto.getExpireSeconds() : defaultExpireSeconds;
        Boolean ok = redisTemplate.opsForValue().setIfAbsent(key, "1", expire, TimeUnit.SECONDS);
        log.debug("幂等校验 key={} ok={}", key, ok);
        return Boolean.TRUE.equals(ok);
    }

    @Override
    public boolean release(IdempotentDTO dto) {
        String key = buildKey(dto);
        Boolean deleted = redisTemplate.delete(key);
        return Boolean.TRUE.equals(deleted);
    }

    private String buildKey(IdempotentDTO dto) {
        return StrUtil.join(":", keyPrefix, dto.getTaskType(), dto.getBizKey());
    }
}
