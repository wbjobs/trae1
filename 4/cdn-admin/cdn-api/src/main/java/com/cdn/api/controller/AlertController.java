package com.cdn.api.controller;

import com.baomidou.mybatisplus.extension.plugins.pagination.Page;
import com.cdn.common.result.R;
import com.cdn.domain.entity.AlertRecord;
import com.cdn.service.RefreshService;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.Map;

@Api(tags = "告警管理")
@RestController
@RequestMapping("/api/alert")
@RequiredArgsConstructor
public class AlertController {

    private final RefreshService refreshService;

    @ApiOperation("分页查询告警记录")
    @GetMapping("/page")
    public R<Page<AlertRecord>> page(@RequestParam(defaultValue = "1") int pageNum,
                                     @RequestParam(defaultValue = "10") int pageSize) {
        return R.ok(refreshService.alertPage(pageNum, pageSize));
    }

    @ApiOperation("处理告警")
    @PostMapping("/handle/{id}")
    public R<Void> handle(@PathVariable Long id, @RequestBody Map<String, String> body) {
        refreshService.handleAlert(id, body.get("result"));
        return R.ok();
    }
}
