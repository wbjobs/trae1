package com.distributed.task.common.dto;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import java.io.Serializable;

@Data
@ApiModel(value = "幂等校验DTO")
public class IdempotentDTO implements Serializable {

    private static final long serialVersionUID = 1L;

    @ApiModelProperty(value = "任务类型", required = true)
    private String taskType;

    @ApiModelProperty(value = "业务唯一键", required = true)
    private String bizKey;

    @ApiModelProperty(value = "过期秒数", example = "86400")
    private Integer expireSeconds;
}
