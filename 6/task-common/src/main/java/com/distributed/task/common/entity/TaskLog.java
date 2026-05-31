package com.distributed.task.common.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_task_log")
public class TaskLog implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(value = "id", type = IdType.AUTO)
    private Long id;

    @TableField("task_no")
    private String taskNo;

    @TableField("task_type")
    private String taskType;

    @TableField("execute_node")
    private String executeNode;

    @TableField("execute_status")
    private Integer executeStatus;

    @TableField("execute_result")
    private String executeResult;

    @TableField("error_message")
    private String errorMessage;

    @TableField("cost_ms")
    private Long costMs;

    @TableField("retry_no")
    private Integer retryNo;

    @TableField("create_time")
    private LocalDateTime createTime;
}
