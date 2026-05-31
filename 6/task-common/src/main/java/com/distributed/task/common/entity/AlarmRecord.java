package com.distributed.task.common.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_alarm_record")
public class AlarmRecord implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(value = "id", type = IdType.AUTO)
    private Long id;

    @TableField("alarm_no")
    private String alarmNo;

    @TableField("task_no")
    private String taskNo;

    @TableField("alarm_type")
    private String alarmType;

    @TableField("alarm_level")
    private Integer alarmLevel;

    @TableField("alarm_content")
    private String alarmContent;

    @TableField("receiver")
    private String receiver;

    @TableField("send_status")
    private Integer sendStatus;

    @TableField("fail_reason")
    private String failReason;

    @TableField("create_time")
    private LocalDateTime createTime;
}
