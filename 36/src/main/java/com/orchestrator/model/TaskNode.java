package com.orchestrator.model;

import java.util.HashMap;
import java.util.Map;

public class TaskNode {

    private String id;
    private String name;
    private TaskType type;
    private Map<String, Object> params;
    private long timeoutMs = 5 * 60 * 1000L;
    private int maxRetries = 3;

    public TaskNode() {
        this.params = new HashMap<>();
    }

    public TaskNode(String id, String name, TaskType type) {
        this.id = id;
        this.name = name;
        this.type = type;
        this.params = new HashMap<>();
    }

    public String getId() { return id; }
    public void setId(String id) { this.id = id; }

    public String getName() { return name; }
    public void setName(String name) { this.name = name; }

    public TaskType getType() { return type; }
    public void setType(TaskType type) { this.type = type; }

    public Map<String, Object> getParams() { return params; }
    public void setParams(Map<String, Object> params) { this.params = params; }

    public long getTimeoutMs() { return timeoutMs; }
    public void setTimeoutMs(long timeoutMs) { this.timeoutMs = timeoutMs; }

    public int getMaxRetries() { return maxRetries; }
    public void setMaxRetries(int maxRetries) { this.maxRetries = maxRetries; }
}
