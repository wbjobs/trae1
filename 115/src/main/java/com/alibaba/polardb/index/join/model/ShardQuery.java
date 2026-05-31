package com.alibaba.polardb.index.join.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.List;
import java.util.Map;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ShardQuery {
    private String shardId;
    private String sql;
    private List<Object> parameters;
    private Map<String, Object> context;
    private long timeoutMs;
}
