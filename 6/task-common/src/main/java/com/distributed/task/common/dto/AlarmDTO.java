package com.distributed.task.common.dto;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import java.io.Serializable;

@Data
@ApiModel(value = "告警DTO")
public class AlarmDTO implements Serializable {

    private static final long serialVersionUID = 1L;

    @ApiModelProperty(value = "任务编号")
    private String taskNo;

    @ApiModelProperty(value = "告警类型", example = "TASK_FAIL/TASK_TIMEOUT/SYSTEM")
    private String alarmType;

    @ApiModelProperty(value = "告警等级 1-5")
    private Integer alarmLevel;

    @ApiModelProperty(value = "告警内容")
    private String alarmContent;

    @ApiModelProperty(value = "接收人")
    private String receiver;
}
