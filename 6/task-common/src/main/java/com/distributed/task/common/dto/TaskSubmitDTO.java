package com.distributed.task.common.dto;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import javax.validation.constraints.NotBlank;
import java.io.Serializable;

@Data
@ApiModel(value = "任务提交DTO")
public class TaskSubmitDTO implements Serializable {

    private static final long serialVersionUID = 1L;

    @NotBlank(message = "任务类型不能为空")
    @ApiModelProperty(value = "任务类型", required = true, example = "ORDER_NOTIFY")
    private String taskType;

    @NotBlank(message = "业务唯一键不能为空")
    @ApiModelProperty(value = "业务唯一键（幂等键）", required = true, example = "ORDER_123456")
    private String bizKey;

    @ApiModelProperty(value = "任务载荷（JSON）", example = "{\"orderId\":123456}")
    private String taskPayload;

    @ApiModelProperty(value = "回调URL", example = "http://localhost:8080/callback")
    private String callbackUrl;

    @ApiModelProperty(value = "最大重试次数", example = "5")
    private Integer maxRetryCount;

    @ApiModelProperty(value = "超时秒数", example = "300")
    private Integer timeoutSeconds;

    @ApiModelProperty(value = "幂等键过期秒数", example = "86400")
    private Integer idempotentExpireSeconds;

    @ApiModelProperty(value = "任务优先级 1最高 5最低", example = "3")
    private Integer priority;
}
