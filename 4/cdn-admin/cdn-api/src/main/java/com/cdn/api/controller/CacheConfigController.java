package com.cdn.api.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.common.result.R;
import com.cdn.domain.dto.CacheConfigDTO;
import com.cdn.domain.entity.CacheConfig;
import com.cdn.service.CacheConfigService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import javax.validation.Valid;

@Api(tags = "缓存配置")
@RestController
@RequestMapping("/api/cache-config")
@RequiredArgsConstructor
public class CacheConfigController {

    private final CacheConfigService cacheConfigService;

    @ApiOperation("分页查询缓存配置")
    @GetMapping("/page")
    public R<Page<CacheConfig>> page(@RequestParam(defaultValue = "1") int pageNum,
                                     @RequestParam(defaultValue = "10") int pageSize,
                                     @RequestParam(required = false) String keyword) {
        return R.ok(cacheConfigService.page(pageNum, pageSize, keyword));
    }

    @ApiOperation("新增/更新缓存配置")
    @PostMapping
    public R<CacheConfig> save(@Valid @RequestBody CacheConfigDTO dto) {
        return R.ok(cacheConfigService.save(dto));
    }

    @ApiOperation("删除缓存配置")
    @DeleteMapping("/{id}")
    public R<Void> delete(@PathVariable Long id) {
        cacheConfigService.delete(id);
        return R.ok();
    }
}
