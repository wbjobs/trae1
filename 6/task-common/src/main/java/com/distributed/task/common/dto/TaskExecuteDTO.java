package com.distributed.task.common.dto;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import java.io.Serializable;

@Data
@ApiModel(value = "任务执行结果DTO")
public class TaskExecuteDTO implements Serializable {

    private static final long serialVersionUID = 1L;

    @ApiModelProperty(value = "任务编号", required = true)
    private String taskNo;

    @ApiModelProperty(value = "执行状态 1成功 0失败", required = true)
    private Integer success;

    @ApiModelProperty(value = "执行结果")
    private String result;

    @ApiModelProperty(value = "错误信息")
    private String errorMessage;

    @ApiModelProperty(value = "执行耗时(ms)")
    private Long costMs;

    @ApiModelProperty(value = "执行节点")
    private String executeNode;

    @ApiModelProperty(value = "是否最终失败(耗尽重试)")
    private Boolean finalFail;
}
