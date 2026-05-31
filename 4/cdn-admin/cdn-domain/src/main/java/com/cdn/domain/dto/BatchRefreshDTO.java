package com.cdn.domain.dto;

import lombok.Data;
import javax.validation.constraints.NotEmpty;
import javax.validation.constraints.NotNull;
import java.util.List;

@Data
public class BatchRefreshDTO {
    @NotEmpty(message = "资源URL列表不能为空")
    private List<String> resourceUrls;
    @NotNull(message = "刷新类型不能为空")
    private Integer refreshType;
    private String operator;
}
