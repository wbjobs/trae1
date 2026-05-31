package com.cdn.api.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.common.result.R;
import com.cdn.domain.dto.CdnResourceDTO;
import com.cdn.domain.entity.CdnResource;
import com.cdn.service.ResourceService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import javax.validation.Valid;

@Api(tags = "资源管理")
@RestController
@RequestMapping("/api/resource")
@RequiredArgsConstructor
public class ResourceController {

    private final ResourceService resourceService;

    @ApiOperation("分页查询资源列表")
    @GetMapping("/page")
    public R<Page<CdnResource>> page(@RequestParam(defaultValue = "1") int pageNum,
                                    @RequestParam(defaultValue = "10") int pageSize,
                                    @RequestParam(required = false) String keyword) {
        return R.ok(resourceService.page(pageNum, pageSize, keyword));
    }

    @ApiOperation("获取资源详情")
    @GetMapping("/{id}")
    public R<CdnResource> get(@PathVariable Long id) {
        return R.ok(resourceService.getById(id));
    }

    @ApiOperation("新增/更新资源")
    @PostMapping
    public R<CdnResource> save(@Valid @RequestBody CdnResourceDTO dto) {
        return R.ok(resourceService.save(dto));
    }

    @ApiOperation("删除资源")
    @DeleteMapping("/{id}")
    public R<Void> delete(@PathVariable Long id) {
        resourceService.delete(id);
        return R.ok();
    }
}
