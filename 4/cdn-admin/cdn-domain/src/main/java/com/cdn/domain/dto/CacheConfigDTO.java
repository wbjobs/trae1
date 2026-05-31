package com.cdn.domain.dto;

import lombok.Data;
import javax.validation.constraints.NotBlank;

@Data
public class CacheConfigDTO {
    private Long id;
    @NotBlank(message = "配置键不能为空")
    private String configKey;
    @NotBlank(message = "资源匹配规则不能为空")
    private String resourcePattern;
    private Integer ttlSeconds;
    private Integer cacheStrategy;
    private Integer status;
    private String remark;
}
