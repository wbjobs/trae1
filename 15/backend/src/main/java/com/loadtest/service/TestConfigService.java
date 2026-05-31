package com.loadtest.service;

import com.loadtest.dto.TestConfigDTO;
import com.loadtest.entity.TestConfig;
import com.loadtest.repository.TestConfigRepository;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.net.MalformedURLException;
import java.net.URL;
import java.util.List;
import java.util.regex.Pattern;
import java.util.stream.Collectors;

@Slf4j
@Service
@RequiredArgsConstructor
public class TestConfigService {

    private final TestConfigRepository testConfigRepository;

    private static final int MAX_THREAD_COUNT = 5000;
    private static final int MAX_RAMP_UP_TIME = 3600;
    private static final int MAX_LOOP_COUNT = 1000000;
    private static final int MAX_DURATION = 86400;
    private static final int MAX_PORT = 65535;
    private static final int MIN_PORT = 1;
    private static final int MAX_NAME_LENGTH = 200;
    private static final int MAX_URL_LENGTH = 1000;
    private static final int MAX_DESCRIPTION_LENGTH = 500;
    private static final int MAX_HEADERS_LENGTH = 5000;
    private static final int MAX_BODY_LENGTH = 10000;
    private static final int MAX_DOMAIN_LENGTH = 100;
    private static final int MAX_PATH_LENGTH = 500;

    private static final Pattern URL_PATTERN = Pattern.compile(
            "^https?://[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?(\\.[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?)*(:\\d+)?(/.*)?$"
    );

    public List<TestConfigDTO> getAllConfigs() {
        return testConfigRepository.findAllByOrderByCreatedAtDesc()
                .stream()
                .map(this::toDTO)
                .collect(Collectors.toList());
    }

    public TestConfigDTO getConfigById(Long id) {
        return testConfigRepository.findById(id)
                .map(this::toDTO)
                .orElseThrow(() -> new RuntimeException("Config not found: " + id));
    }

    public TestConfig getConfigEntityById(Long id) {
        return testConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("Config not found: " + id));
    }

    @Transactional
    public TestConfigDTO createConfig(TestConfigDTO dto) {
        validateConfig(dto);
        TestConfig config = toEntity(dto);
        config.setId(null);
        TestConfig saved = testConfigRepository.save(config);
        log.info("Created test config: {}", saved.getName());
        return toDTO(saved);
    }

    @Transactional
    public TestConfigDTO updateConfig(Long id, TestConfigDTO dto) {
        validateConfig(dto);
        TestConfig existing = testConfigRepository.findById(id)
                .orElseThrow(() -> new RuntimeException("Config not found: " + id));

        existing.setName(dto.getName());
        existing.setDescription(dto.getDescription());
        existing.setMethod(TestConfig.HttpMethod.valueOf(dto.getMethod()));
        existing.setUrl(dto.getUrl());
        existing.setHeaders(dto.getHeaders());
        existing.setRequestBody(dto.getRequestBody());
        existing.setThreadCount(dto.getThreadCount());
        existing.setRampUpTime(dto.getRampUpTime());
        existing.setLoopCount(dto.getLoopCount());
        existing.setDuration(dto.getDuration());
        existing.setUseLoopCount(dto.getUseLoopCount());
        existing.setProtocol(dto.getProtocol());
        existing.setPort(dto.getPort());
        existing.setPath(dto.getPath());
        existing.setDomain(dto.getDomain());
        existing.setSimulateDelay(dto.getSimulateDelay() != null ? dto.getSimulateDelay() : false);
        existing.setDelayMinMs(dto.getDelayMinMs());
        existing.setDelayMaxMs(dto.getDelayMaxMs());
        existing.setSimulateTimeout(dto.getSimulateTimeout() != null ? dto.getSimulateTimeout() : false);
        existing.setTimeoutProbability(dto.getTimeoutProbability());
        existing.setSimulateError(dto.getSimulateError() != null ? dto.getSimulateError() : false);
        existing.setErrorProbability(dto.getErrorProbability());
        existing.setErrorStatusCodes(dto.getErrorStatusCodes());
        existing.setConnectionTimeout(dto.getConnectionTimeout() != null ? dto.getConnectionTimeout() : 10000);
        existing.setResponseTimeout(dto.getResponseTimeout() != null ? dto.getResponseTimeout() : 30000);

        TestConfig saved = testConfigRepository.save(existing);
        log.info("Updated test config: {}", saved.getName());
        return toDTO(saved);
    }

    @Transactional
    public void deleteConfig(Long id) {
        if (!testConfigRepository.existsById(id)) {
            throw new RuntimeException("Config not found: " + id);
        }
        testConfigRepository.deleteById(id);
        log.info("Deleted test config: {}", id);
    }

    private void validateConfig(TestConfigDTO dto) {
        List<String> errors = new java.util.ArrayList<>();

        if (dto.getName() == null || dto.getName().trim().isEmpty()) {
            errors.add("配置名称不能为空");
        } else if (dto.getName().length() > MAX_NAME_LENGTH) {
            errors.add("配置名称长度不能超过 " + MAX_NAME_LENGTH + " 字符");
        }

        if (dto.getDescription() != null && dto.getDescription().length() > MAX_DESCRIPTION_LENGTH) {
            errors.add("配置描述长度不能超过 " + MAX_DESCRIPTION_LENGTH + " 字符");
        }

        if (dto.getMethod() == null || dto.getMethod().trim().isEmpty()) {
            errors.add("请求方法不能为空");
        } else {
            try {
                TestConfig.HttpMethod.valueOf(dto.getMethod());
            } catch (IllegalArgumentException e) {
                errors.add("无效的请求方法: " + dto.getMethod());
            }
        }

        if (dto.getUrl() == null || dto.getUrl().trim().isEmpty()) {
            errors.add("请求URL不能为空");
        } else if (dto.getUrl().length() > MAX_URL_LENGTH) {
            errors.add("请求URL长度不能超过 " + MAX_URL_LENGTH + " 字符");
        } else if (!isValidUrl(dto.getUrl())) {
            errors.add("无效的URL格式");
        }

        if (dto.getHeaders() != null && dto.getHeaders().length() > MAX_HEADERS_LENGTH) {
            errors.add("请求头长度不能超过 " + MAX_HEADERS_LENGTH + " 字符");
        }

        if (dto.getRequestBody() != null && dto.getRequestBody().length() > MAX_BODY_LENGTH) {
            errors.add("请求体长度不能超过 " + MAX_BODY_LENGTH + " 字符");
        }

        if (dto.getThreadCount() == null) {
            errors.add("线程数不能为空");
        } else if (dto.getThreadCount() < 1) {
            errors.add("线程数必须大于 0");
        } else if (dto.getThreadCount() > MAX_THREAD_COUNT) {
            errors.add("线程数不能超过 " + MAX_THREAD_COUNT);
        }

        if (dto.getRampUpTime() == null) {
            errors.add("启动时间不能为空");
        } else if (dto.getRampUpTime() < 1) {
            errors.add("启动时间必须大于 0 秒");
        } else if (dto.getRampUpTime() > MAX_RAMP_UP_TIME) {
            errors.add("启动时间不能超过 " + MAX_RAMP_UP_TIME + " 秒");
        }

        if (dto.getUseLoopCount() == null) {
            dto.setUseLoopCount(true);
        }

        if (Boolean.TRUE.equals(dto.getUseLoopCount())) {
            if (dto.getLoopCount() == null) {
                errors.add("循环次数不能为空");
            } else if (dto.getLoopCount() < 1) {
                errors.add("循环次数必须大于 0");
            } else if (dto.getLoopCount() > MAX_LOOP_COUNT) {
                errors.add("循环次数不能超过 " + MAX_LOOP_COUNT);
            }
        } else {
            if (dto.getDuration() == null) {
                errors.add("持续时间不能为空");
            } else if (dto.getDuration() < 1) {
                errors.add("持续时间必须大于 0 秒");
            } else if (dto.getDuration() > MAX_DURATION) {
                errors.add("持续时间不能超过 " + MAX_DURATION + " 秒 (24小时)");
            }
        }

        if (dto.getProtocol() != null && !dto.getProtocol().isEmpty()) {
            if (!dto.getProtocol().equalsIgnoreCase("http") && !dto.getProtocol().equalsIgnoreCase("https")) {
                errors.add("协议只能是 http 或 https");
            }
        }

        if (dto.getPort() != null) {
            if (dto.getPort() < MIN_PORT || dto.getPort() > MAX_PORT) {
                errors.add("端口号必须在 " + MIN_PORT + " 到 " + MAX_PORT + " 之间");
            }
        }

        if (dto.getDomain() != null && dto.getDomain().length() > MAX_DOMAIN_LENGTH) {
            errors.add("域名长度不能超过 " + MAX_DOMAIN_LENGTH + " 字符");
        }

        if (dto.getPath() != null && dto.getPath().length() > MAX_PATH_LENGTH) {
            errors.add("请求路径长度不能超过 " + MAX_PATH_LENGTH + " 字符");
        }

        if (!errors.isEmpty()) {
            throw new IllegalArgumentException("参数校验失败: " + String.join("; ", errors));
        }
    }

    private boolean isValidUrl(String url) {
        try {
            new URL(url);
            return true;
        } catch (MalformedURLException e) {
            return URL_PATTERN.matcher(url).matches();
        }
    }

    private TestConfigDTO toDTO(TestConfig config) {
        return TestConfigDTO.builder()
                .id(config.getId())
                .name(config.getName())
                .description(config.getDescription())
                .method(config.getMethod().name())
                .url(config.getUrl())
                .headers(config.getHeaders())
                .requestBody(config.getRequestBody())
                .threadCount(config.getThreadCount())
                .rampUpTime(config.getRampUpTime())
                .loopCount(config.getLoopCount())
                .duration(config.getDuration())
                .useLoopCount(config.getUseLoopCount())
                .protocol(config.getProtocol())
                .port(config.getPort())
                .path(config.getPath())
                .domain(config.getDomain())
                .simulateDelay(config.getSimulateDelay())
                .delayMinMs(config.getDelayMinMs())
                .delayMaxMs(config.getDelayMaxMs())
                .simulateTimeout(config.getSimulateTimeout())
                .timeoutProbability(config.getTimeoutProbability())
                .simulateError(config.getSimulateError())
                .errorProbability(config.getErrorProbability())
                .errorStatusCodes(config.getErrorStatusCodes())
                .connectionTimeout(config.getConnectionTimeout())
                .responseTimeout(config.getResponseTimeout())
                .createdAt(config.getCreatedAt() != null ? config.getCreatedAt().toString() : null)
                .updatedAt(config.getUpdatedAt() != null ? config.getUpdatedAt().toString() : null)
                .build();
    }

    private TestConfig toEntity(TestConfigDTO dto) {
        return TestConfig.builder()
                .name(dto.getName())
                .description(dto.getDescription())
                .method(TestConfig.HttpMethod.valueOf(dto.getMethod()))
                .url(dto.getUrl())
                .headers(dto.getHeaders())
                .requestBody(dto.getRequestBody())
                .threadCount(dto.getThreadCount())
                .rampUpTime(dto.getRampUpTime())
                .loopCount(dto.getLoopCount())
                .duration(dto.getDuration())
                .useLoopCount(dto.getUseLoopCount())
                .protocol(dto.getProtocol())
                .port(dto.getPort())
                .path(dto.getPath())
                .domain(dto.getDomain())
                .simulateDelay(dto.getSimulateDelay() != null ? dto.getSimulateDelay() : false)
                .delayMinMs(dto.getDelayMinMs())
                .delayMaxMs(dto.getDelayMaxMs())
                .simulateTimeout(dto.getSimulateTimeout() != null ? dto.getSimulateTimeout() : false)
                .timeoutProbability(dto.getTimeoutProbability())
                .simulateError(dto.getSimulateError() != null ? dto.getSimulateError() : false)
                .errorProbability(dto.getErrorProbability())
                .errorStatusCodes(dto.getErrorStatusCodes())
                .connectionTimeout(dto.getConnectionTimeout() != null ? dto.getConnectionTimeout() : 10000)
                .responseTimeout(dto.getResponseTimeout() != null ? dto.getResponseTimeout() : 30000)
                .build();
    }
}
