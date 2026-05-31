package com.distributed.task.common.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import com.distributed.task.common.enums.TaskStatus;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_task_info")
public class TaskInfo implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(value = "id", type = IdType.AUTO)
    private Long id;

    @TableField("task_no")
    private String taskNo;

    @TableField("task_type")
    private String taskType;

    @TableField("biz_key")
    private String bizKey;

    @TableField("task_payload")
    private String taskPayload;

    @TableField("callback_url")
    private String callbackUrl;

    @TableField("status")
    private Integer status;

    @TableField("retry_count")
    private Integer retryCount;

    @TableField("max_retry_count")
    private Integer maxRetryCount;

    @TableField("retry_level")
    private Integer retryLevel;

    @TableField("timeout_seconds")
    private Integer timeoutSeconds;

    @TableField("next_retry_time")
    private LocalDateTime nextRetryTime;

    @TableField("first_execute_time")
    private LocalDateTime firstExecuteTime;

    @TableField("last_execute_time")
    private LocalDateTime lastExecuteTime;

    @TableField("owner_node")
    private String ownerNode;

    @TableField("priority")
    private Integer priority;

    @TableField("trace_id")
    private String traceId;

    @TableField("remark")
    private String remark;

    @TableField("create_time")
    private LocalDateTime createTime;

    @TableField("update_time")
    private LocalDateTime updateTime;

    @TableField("deleted")
    private Integer deleted;

    public TaskStatus getTaskStatus() {
        return TaskStatus.of(status);
    }
}
