package com.cdn.domain.dto;

import lombok.Data;
import javax.validation.constraints.NotBlank;

@Data
public class CdnResourceDTO {
    private Long id;
    @NotBlank(message = "资源URL不能为空")
    private String resourceUrl;
    private String resourceType;
    private String resourceName;
    private Long fileSize;
    private Integer status;
    private String remark;
}
