package com.loadtest.websocket;

import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.messaging.simp.SimpMessagingTemplate;
import org.springframework.stereotype.Component;

import java.util.Map;

@Slf4j
@Component
@RequiredArgsConstructor
public class WebSocketPushService {

    private final SimpMessagingTemplate messagingTemplate;

    public void pushTaskStatus(Long taskId, String status, Map<String, Object> data) {
        Map<String, Object> message = Map.of(
                "taskId", taskId,
                "status", status,
                "data", data != null ? data : Map.of(),
                "timestamp", System.currentTimeMillis()
        );
        messagingTemplate.convertAndSend("/topic/task-status/" + taskId, message);
        messagingTemplate.convertAndSend("/topic/task-updates", Map.of(
                "type", "TASK_STATUS_UPDATE",
                "taskId", taskId,
                "status", status,
                "timestamp", System.currentTimeMillis()
        ));
        log.debug("Pushed task status for task {}: {}", taskId, status);
    }

    public void pushMetricsUpdate(Long taskId, Map<String, Object> metrics) {
        Map<String, Object> message = Map.of(
                "taskId", taskId,
                "metrics", metrics,
                "timestamp", System.currentTimeMillis()
        );
        messagingTemplate.convertAndSend("/topic/metrics/" + taskId, message);
    }

    public void pushResultUpdate(Long taskId, Map<String, Object> result) {
        Map<String, Object> message = Map.of(
                "taskId", taskId,
                "result", result,
                "timestamp", System.currentTimeMillis()
        );
        messagingTemplate.convertAndSend("/topic/results/" + taskId, message);
    }

    public void pushSystemStats(Map<String, Object> stats) {
        messagingTemplate.convertAndSend("/topic/system-stats", stats);
    }
}
