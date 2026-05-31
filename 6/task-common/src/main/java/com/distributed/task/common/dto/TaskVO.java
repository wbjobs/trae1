package com.distributed.task.common.dto;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@ApiModel(value = "任务VO")
public class TaskVO implements Serializable {

    private static final long serialVersionUID = 1L;

    @ApiModelProperty(value = "主键ID")
    private Long id;

    @ApiModelProperty(value = "任务编号")
    private String taskNo;

    @ApiModelProperty(value = "任务类型")
    private String taskType;

    @ApiModelProperty(value = "业务唯一键")
    private String bizKey;

    @ApiModelProperty(value = "任务状态")
    private Integer status;

    @ApiModelProperty(value = "任务状态描述")
    private String statusDesc;

    @ApiModelProperty(value = "已重试次数")
    private Integer retryCount;

    @ApiModelProperty(value = "最大重试次数")
    private Integer maxRetryCount;

    @ApiModelProperty(value = "当前重试等级")
    private Integer retryLevel;

    @ApiModelProperty(value = "超时秒数")
    private Integer timeoutSeconds;

    @ApiModelProperty(value = "下次重试时间")
    private LocalDateTime nextRetryTime;

    @ApiModelProperty(value = "首次执行时间")
    private LocalDateTime firstExecuteTime;

    @ApiModelProperty(value = "最后执行时间")
    private LocalDateTime lastExecuteTime;

    @ApiModelProperty(value = "所属节点")
    private String ownerNode;

    @ApiModelProperty(value = "任务优先级 1最高 5最低")
    private Integer priority;

    @ApiModelProperty(value = "链路追踪ID")
    private String traceId;

    @ApiModelProperty(value = "备注")
    private String remark;

    @ApiModelProperty(value = "创建时间")
    private LocalDateTime createTime;
}
