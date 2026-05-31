package com.alibaba.polardb.index.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class BinlogPosition {
    private String journalName;
    private long position;
    private long timestamp;
    private String serverId;

    public String toString() {
        return journalName + ":" + position + "@" + serverId;
    }
}
