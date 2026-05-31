package com.cdn.api.controller;

import com.cdn.common.result.R;
import com.cdn.service.cache.CacheService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.Map;

@Api(tags = "缓存统计")
@RestController
@RequestMapping("/api/stats")
@RequiredArgsConstructor
public class StatsController {

    private final CacheService cacheService;

    @ApiOperation("获取缓存命中率统计")
    @GetMapping("/hit-rate")
    public R<Map<String, Object>> hitRate() {
        return R.ok(cacheService.getStats());
    }

    @ApiOperation("手动清理异常资源缓存")
    @PostMapping("/clean-abnormal")
    public R<Void> cleanAbnormal() {
        cacheService.cleanAbnormalResources();
        return R.ok();
    }
}
