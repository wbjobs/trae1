package com.sharding.sync.incremental.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_incremental_event")
public class IncrementalEvent implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(type = IdType.AUTO)
    private Long id;

    @TableField("event_id")
    private String eventId;

    @TableField("logic_table")
    private String logicTable;

    @TableField("action")
    private String action;

    @TableField("pk_value")
    private String pkValue;

    @TableField("before_data")
    private String beforeData;

    @TableField("after_data")
    private String afterData;

    @TableField("binlog_file")
    private String binlogFile;

    @TableField("binlog_position")
    private Long binlogPosition;

    @TableField("status")
    private Integer status;

    @TableField("retry_count")
    private Integer retryCount;

    @TableField("error_msg")
    private String errorMsg;

    @TableField("create_time")
    private LocalDateTime createTime;

    @TableField("update_time")
    private LocalDateTime updateTime;
}
