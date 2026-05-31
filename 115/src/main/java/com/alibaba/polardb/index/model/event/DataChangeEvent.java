package com.alibaba.polardb.index.model.event;

import com.alibaba.otter.canal.protocol.CanalEntry;
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
public class DataChangeEvent {
    private String shardId;
    private String database;
    private String table;
    private CanalEntry.EventType eventType;
    private Map<String, String> beforeColumns;
    private Map<String, String> afterColumns;
    private List<String> primaryKeys;
    private String globalId;
    private String shardKey;
    private long executeTime;
    private long receiveTime;
    private String binlogJournal;
    private long binlogPosition;
}
