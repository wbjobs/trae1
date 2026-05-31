package com.alibaba.polardb.index.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ShardInfo {
    private String shardId;
    private String shardName;
    private int status;
    private long totalRows;
    private long syncRows;
    private long delayMs;
}
