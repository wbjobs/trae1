package com.orchestrator.model;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class DAGDefinition {

    private String id;
    private String name;
    private Map<String, TaskNode> tasks;
    private Map<String, List<String>> dependencies;

    public DAGDefinition() {
        this.tasks = new HashMap<>();
        this.dependencies = new HashMap<>();
    }

    public String getId() { return id; }
    public void setId(String id) { this.id = id; }

    public String getName() { return name; }
    public void setName(String name) { this.name = name; }

    public Map<String, TaskNode> getTasks() { return tasks; }
    public void setTasks(Map<String, TaskNode> tasks) { this.tasks = tasks; }

    public Map<String, List<String>> getDependencies() { return dependencies; }
    public void setDependencies(Map<String, List<String>> dependencies) { this.dependencies = dependencies; }

    public void addTask(TaskNode task) {
        tasks.put(task.getId(), task);
        if (!dependencies.containsKey(task.getId())) {
            dependencies.put(task.getId(), new ArrayList<>());
        }
    }

    public void addDependency(String taskId, String dependencyId) {
        dependencies.computeIfAbsent(taskId, k -> new ArrayList<>()).add(dependencyId);
    }

    public List<String> getDependencies(String taskId) {
        return dependencies.getOrDefault(taskId, new ArrayList<>());
    }
}
