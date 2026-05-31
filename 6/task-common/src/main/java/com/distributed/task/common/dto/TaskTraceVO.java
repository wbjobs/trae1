package com.distributed.task.common.dto;

import com.distributed.task.common.entity.AlarmRecord;
import com.distributed.task.common.entity.TaskLog;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;

import java.io.Serializable;
import java.util.List;

@Data
@ApiModel(value = "任务链路追踪VO")
public class TaskTraceVO implements Serializable {

    private static final long serialVersionUID = 1L;

    @ApiModelProperty(value = "任务基础信息")
    private TaskVO taskInfo;

    @ApiModelProperty(value = "执行日志列表")
    private List<TaskLog> logs;

    @ApiModelProperty(value = "告警记录列表")
    private List<AlarmRecord> alarms;

    @ApiModelProperty(value = "任务生命周期阶段数量")
    private Integer stageCount;
}
