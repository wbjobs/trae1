package com.sharding.sync.sync.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_sync_task")
public class SyncTask implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(type = IdType.AUTO)
    private Long id;

    @TableField("task_no")
    private String taskNo;

    @TableField("task_name")
    private String taskName;

    @TableField("sync_type")
    private String syncType;

    @TableField("logic_table")
    private String logicTable;

    @TableField("source_ds")
    private String sourceDs;

    @TableField("target_ds")
    private String targetDs;

    @TableField("status")
    private String status;

    @TableField("trigger_mode")
    private String triggerMode;

    @TableField("total_count")
    private Long totalCount;

    @TableField("success_count")
    private Long successCount;

    @TableField("fail_count")
    private Long failCount;

    @TableField("start_time")
    private LocalDateTime startTime;

    @TableField("end_time")
    private LocalDateTime endTime;

    @TableField("error_msg")
    private String errorMsg;

    @TableField("params")
    private String params;

    @TableField("retry_of")
    private Long retryOf;

    @TableField("create_time")
    private LocalDateTime createTime;

    @TableField("update_time")
    private LocalDateTime updateTime;
}
