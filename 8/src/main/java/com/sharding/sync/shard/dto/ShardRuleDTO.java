package com.sharding.sync.shard.dto;

import lombok.Data;

import javax.validation.constraints.NotBlank;
import javax.validation.constraints.NotNull;
import java.util.List;

@Data
public class ShardRuleDTO {

    private Long id;

    @NotBlank(message = "逻辑表名不能为空")
    private String logicTable;

    @NotBlank(message = "分片键不能为空")
    private String shardingColumn;

    @NotBlank(message = "分片算法不能为空")
    private String algorithm = "mod-long";

    @NotNull(message = "分片数不能为空")
    private Integer shardCount;

    private String primaryKey = "id";

    private List<String> shardNodes;

    private Integer status = 1;

    private String remark;
}
