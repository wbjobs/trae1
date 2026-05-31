package com.alibaba.polardb.index.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.Date;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class GlobalIndex {
    private String globalId;
    private String shardKey;
    private String shardId;
    private Date gmtCreate;
    private Date gmtModified;
}
