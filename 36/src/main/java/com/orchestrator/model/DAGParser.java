package com.orchestrator.model;

import java.util.*;

public class DAGParser {

    public static List<String> topologicalSort(DAGDefinition dag) {
        Map<String, TaskNode> tasks = dag.getTasks();
        Map<String, List<String>> dependencies = dag.getDependencies();

        Map<String, Integer> inDegree = new HashMap<>();
        for (String taskId : tasks.keySet()) {
            inDegree.put(taskId, 0);
        }

        Map<String, List<String>> adjacency = new HashMap<>();
        for (String taskId : tasks.keySet()) {
            adjacency.put(taskId, new ArrayList<>());
        }

        for (Map.Entry<String, List<String>> entry : dependencies.entrySet()) {
            String taskId = entry.getKey();
            for (String depId : entry.getValue()) {
                adjacency.get(depId).add(taskId);
                inDegree.put(taskId, inDegree.get(taskId) + 1);
            }
        }

        Queue<String> queue = new LinkedList<>();
        for (Map.Entry<String, Integer> entry : inDegree.entrySet()) {
            if (entry.getValue() == 0) {
                queue.add(entry.getKey());
            }
        }

        List<String> result = new ArrayList<>();
        while (!queue.isEmpty()) {
            String taskId = queue.poll();
            result.add(taskId);
            for (String neighbor : adjacency.get(taskId)) {
                inDegree.put(neighbor, inDegree.get(neighbor) - 1);
                if (inDegree.get(neighbor) == 0) {
                    queue.add(neighbor);
                }
            }
        }

        if (result.size() != tasks.size()) {
            throw new IllegalArgumentException("DAG contains a cycle, cannot perform topological sort");
        }

        return result;
    }

    public static List<String> getReadyTasks(DAGDefinition dag, Set<String> completedTasks) {
        List<String> ready = new ArrayList<>();
        for (String taskId : dag.getTasks().keySet()) {
            if (completedTasks.contains(taskId)) {
                continue;
            }
            List<String> deps = dag.getDependencies(taskId);
            boolean allDepsCompleted = true;
            for (String dep : deps) {
                if (!completedTasks.contains(dep)) {
                    allDepsCompleted = false;
                    break;
                }
            }
            if (allDepsCompleted) {
                ready.add(taskId);
            }
        }
        return ready;
    }
}
