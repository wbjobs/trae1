package com.sharding.sync.incremental.service;

import com.alibaba.fastjson2.JSON;
import com.sharding.sync.config.MyCatProperties;
import com.sharding.sync.incremental.entity.BinlogPosition;
import com.sharding.sync.incremental.entity.IncrementalEvent;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import com.sharding.sync.sync.entity.SyncTask;
import com.sharding.sync.sync.service.DataSyncService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

@Slf4j
@Component
@RequiredArgsConstructor
public class IncrementalSyncWorker {

    private final IncrementalService incrementalService;
    private final ShardRuleService shardRuleService;
    private final DataSyncService dataSyncService;
    private final MyCatProperties myCatProperties;

    @Scheduled(fixedDelayString = "${mycat.binlog.poll-interval-ms:1000}")
    public void pollAndApply() {
        if (!myCatProperties.getBinlog().isEnabled()) {
            return;
        }
        List<ShardRule> rules = shardRuleService.listAll();
        for (ShardRule rule : rules) {
            if (rule.getStatus() == null || rule.getStatus() != 1) {
                continue;
            }
            try {
                List<IncrementalEvent> events = incrementalService.listPendingEvents(rule.getLogicTable(), 200);
                if (events.isEmpty()) {
                    continue;
                }
                applyEvents(rule, events);
            } catch (Exception e) {
                log.warn("增量事件应用失败 table={}: {}", rule.getLogicTable(), e.getMessage());
            }
        }
    }

    public void applyEvents(ShardRule rule, List<IncrementalEvent> events) {
        if (events == null || events.isEmpty()) {
            return;
        }
        List<Map<String, Object>> list = new ArrayList<>();
        for (IncrementalEvent e : events) {
            Map<String, Object> m = new HashMap<>();
            m.put("action", e.getAction());
            m.put("eventId", e.getId());
            Map<String, Object> row;
            try {
                row = JSON.parseObject(e.getAfterData(), Map.class);
                if (row == null) {
                    row = new HashMap<>();
                    row.put(rule.getPrimaryKey(), e.getPkValue());
                }
            } catch (Exception ex) {
                row = new HashMap<>();
                row.put(rule.getPrimaryKey(), e.getPkValue());
            }
            m.put("row", row);
            list.add(m);
        }
        Map<String, Object> params = new HashMap<>();
        params.put("events", list);
        SyncTask task = dataSyncService.runBinlogSyncForWorker(rule.getLogicTable(), params);
        List<Long> failedEventIds = dataSyncService.getLastBinlogFailedEventIds();
        for (IncrementalEvent e : events) {
            if (failedEventIds != null && failedEventIds.contains(e.getId())) {
                incrementalService.markEventFailed(e.getId(), "binlog sync failed");
            } else {
                incrementalService.markEventSuccess(e.getId());
                updatePosition(rule, e);
            }
        }
    }

    private void updatePosition(ShardRule rule, IncrementalEvent e) {
        if (e.getBinlogFile() == null || e.getBinlogPosition() == null) {
            return;
        }
        BinlogPosition pos = new BinlogPosition();
        pos.setLogicTable(rule.getLogicTable());
        pos.setBinlogFile(e.getBinlogFile());
        pos.setBinlogPosition(e.getBinlogPosition());
        incrementalService.savePosition(pos);
    }

    public void appendEvent(String logicTable, String action, String pkValue,
                            String beforeData, String afterData, String binlogFile, Long position) {
        IncrementalEvent event = new IncrementalEvent();
        event.setEventId(java.util.UUID.randomUUID().toString().replace("-", ""));
        event.setLogicTable(logicTable);
        event.setAction(action);
        event.setPkValue(pkValue);
        event.setBeforeData(beforeData);
        event.setAfterData(afterData);
        event.setBinlogFile(binlogFile);
        event.setBinlogPosition(position);
        incrementalService.saveEvent(event);
    }
}
