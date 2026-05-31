package com.sharding.sync.check.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_check_diff")
public class CheckDiff implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(type = IdType.AUTO)
    private Long id;

    @TableField("task_id")
    private Long taskId;

    @TableField("logic_table")
    private String logicTable;

    @TableField("pk_value")
    private String pkValue;

    @TableField("diff_type")
    private String diffType;

    @TableField("shard_a")
    private String shardA;

    @TableField("shard_b")
    private String shardB;

    @TableField("source_data")
    private String sourceData;

    @TableField("target_data")
    private String targetData;

    @TableField("fix_status")
    private Integer fixStatus;

    @TableField("create_time")
    private LocalDateTime createTime;
}
