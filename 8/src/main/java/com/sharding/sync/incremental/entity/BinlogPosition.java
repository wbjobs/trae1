package com.sharding.sync.incremental.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_binlog_position")
public class BinlogPosition implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(type = IdType.AUTO)
    private Long id;

    @TableField("logic_table")
    private String logicTable;

    @TableField("binlog_file")
    private String binlogFile;

    @TableField("binlog_position")
    private Long binlogPosition;

    @TableField("gtid")
    private String gtid;

    @TableField("server_id")
    private String serverId;

    @TableField("update_time")
    private LocalDateTime updateTime;
}
