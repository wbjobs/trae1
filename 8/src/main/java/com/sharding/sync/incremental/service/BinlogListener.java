package com.sharding.sync.incremental.service;

import com.alibaba.fastjson2.JSON;
import com.sharding.sync.config.MyCatProperties;
import com.sharding.sync.incremental.entity.IncrementalEvent;
import com.sharding.sync.shard.entity.ShardRule;
import com.sharding.sync.shard.service.ShardRuleService;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import javax.annotation.PostConstruct;
import java.util.HashMap;
import java.util.Map;

@Slf4j
@Component
@RequiredArgsConstructor
@ConditionalOnProperty(prefix = "mycat.binlog", name = "enabled", havingValue = "true", matchIfMissing = true)
public class BinlogListener {

    private final MyCatProperties myCatProperties;
    private final IncrementalSyncWorker incrementalSyncWorker;
    private final ShardRuleService shardRuleService;

    private volatile boolean running = false;

    @PostConstruct
    public void init() {
        log.info("Binlog 监听器初始化完成, 模式=定时拉取模拟");
    }

    @Scheduled(fixedDelayString = "${mycat.binlog.poll-interval-ms:1000}")
    public void pollBinlog() {
        if (running) {
            return;
        }
        running = true;
        try {
            for (ShardRule rule : shardRuleService.listAll()) {
                if (rule.getStatus() == null || rule.getStatus() != 1) {
                    continue;
                }
                try {
                    fetchAndPush(rule);
                } catch (Exception e) {
                    log.warn("拉取 binlog 失败 table={}: {}", rule.getLogicTable(), e.getMessage());
                }
            }
        } finally {
            running = false;
        }
    }

    private void fetchAndPush(ShardRule rule) {
        String url = myCatProperties.getAdminUrl() + "/binlog/events?table=" + rule.getLogicTable();
        try {
            String response = HttpUtil.get(url, myCatProperties.getUsername(), myCatProperties.getPassword());
            if (response == null || response.isEmpty()) {
                return;
            }
            Map<String, Object> resp = JSON.parseObject(response, Map.class);
            if (!Boolean.TRUE.equals(resp.get("success"))) {
                return;
            }
            Object events = resp.get("events");
            if (events instanceof java.util.List) {
                for (Object o : (java.util.List<?>) events) {
                    Map<String, Object> evt = (Map<String, Object>) o;
                    incrementalSyncWorker.appendEvent(
                            rule.getLogicTable(),
                            String.valueOf(evt.get("action")),
                            String.valueOf(evt.get("pk")),
                            JSON.toJSONString(evt.get("before")),
                            JSON.toJSONString(evt.get("after")),
                            String.valueOf(evt.get("file")),
                            evt.get("position") == null ? null : ((Number) evt.get("position")).longValue());
                }
            }
        } catch (Exception e) {
            log.debug("binlog 拉取异常(首次可忽略): {}", e.getMessage());
        }
    }

    static class HttpUtil {
        static String get(String url, String user, String pass) {
            return null;
        }
    }
}
