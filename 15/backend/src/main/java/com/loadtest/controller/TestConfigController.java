package com.loadtest.controller;

import com.loadtest.dto.ApiResponse;
import com.loadtest.dto.TestConfigDTO;
import com.loadtest.service.TestConfigService;
import lombok.RequiredArgsConstructor;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/configs")
@RequiredArgsConstructor
public class TestConfigController {

    private final TestConfigService testConfigService;

    @GetMapping
    public ApiResponse<List<TestConfigDTO>> getAllConfigs() {
        return ApiResponse.success(testConfigService.getAllConfigs());
    }

    @GetMapping("/{id}")
    public ApiResponse<TestConfigDTO> getConfigById(@PathVariable Long id) {
        return ApiResponse.success(testConfigService.getConfigById(id));
    }

    @PostMapping
    public ApiResponse<TestConfigDTO> createConfig(@RequestBody TestConfigDTO dto) {
        return ApiResponse.success("配置创建成功", testConfigService.createConfig(dto));
    }

    @PutMapping("/{id}")
    public ApiResponse<TestConfigDTO> updateConfig(@PathVariable Long id, @RequestBody TestConfigDTO dto) {
        return ApiResponse.success("配置更新成功", testConfigService.updateConfig(id, dto));
    }

    @DeleteMapping("/{id}")
    public ApiResponse<Void> deleteConfig(@PathVariable Long id) {
        testConfigService.deleteConfig(id);
        return ApiResponse.success("配置删除成功", null);
    }
}
