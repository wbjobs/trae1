package com.sharding.sync.shard.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableField;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.io.Serializable;
import java.time.LocalDateTime;

@Data
@TableName("t_shard_rule")
public class ShardRule implements Serializable {

    private static final long serialVersionUID = 1L;

    @TableId(type = IdType.AUTO)
    private Long id;

    @TableField("logic_table")
    private String logicTable;

    @TableField("sharding_column")
    private String shardingColumn;

    @TableField("algorithm")
    private String algorithm;

    @TableField("shard_count")
    private Integer shardCount;

    @TableField("primary_key")
    private String primaryKey;

    @TableField("shard_nodes")
    private String shardNodes;

    @TableField("status")
    private Integer status;

    @TableField("remark")
    private String remark;

    @TableField("create_time")
    private LocalDateTime createTime;

    @TableField("update_time")
    private LocalDateTime updateTime;
}
