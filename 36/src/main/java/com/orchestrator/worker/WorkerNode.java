package com.orchestrator.worker;

import com.orchestrator.executor.ExecutionResult;
import com.orchestrator.executor.TaskExecutor;
import com.orchestrator.model.DAGDefinition;
import com.orchestrator.model.TaskNode;
import com.orchestrator.model.TaskRuntime;
import com.orchestrator.model.TaskStatus;
import com.orchestrator.result.ParameterResolver;
import com.orchestrator.result.ResultStore;
import com.orchestrator.zookeeper.ZkPaths;
import com.orchestrator.zookeeper.ZkService;
import org.apache.zookeeper.Watcher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class WorkerNode {

    private static final Logger logger = LoggerFactory.getLogger(WorkerNode.class);

    private final ZkService zkService;
    private final TaskExecutor taskExecutor;
    private final ResultStore resultStore;
    private final String workerId;
    private final ScheduledExecutorService scheduler;
    private final Set<String> executingTasks;

    public WorkerNode(ZkService zkService) {
        this.zkService = zkService;
        this.taskExecutor = new TaskExecutor();
        this.resultStore = new ResultStore(zkService);
        this.workerId = UUID.randomUUID().toString().replace("-", "").substring(0, 10);
        this.scheduler = Executors.newScheduledThreadPool(2);
        this.executingTasks = Collections.synchronizedSet(new HashSet<>());
    }

    public void start() {
        registerWorker();
        scheduler.scheduleAtFixedRate(this::pollAndExecuteTasks, 2, 5, TimeUnit.SECONDS);
        scheduler.scheduleAtFixedRate(this::checkExecutionTimeouts, 10, 10, TimeUnit.SECONDS);
        logger.info("Worker node started, workerId={}", workerId);
    }

    private void registerWorker() {
        String path = ZkPaths.workerPath(workerId);
        zkService.createEphemeral(path, workerId);
        logger.info("Worker registered: {}", path);

        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            logger.info("Worker shutting down, cleaning up...");
            for (String taskKey : executingTasks) {
                String[] parts = taskKey.split("_", 2);
                if (parts.length == 2) {
                    zkService.delete(ZkPaths.runningTaskPath(taskKey));
                }
            }
            zkService.delete(path);
            taskExecutor.shutdown();
        }));
    }

    public void pollAndExecuteTasks() {
        List<String> dagIds = zkService.getChildren(ZkPaths.DAGS);
        for (String dagId : dagIds) {
            String tasksPath = ZkPaths.dagTasksPath(dagId);
            if (!zkService.exists(tasksPath)) continue;

            DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
            if (dag == null) continue;

            for (String taskId : zkService.getChildren(tasksPath)) {
                String taskKey = dagId + "_" + taskId;

                if (executingTasks.contains(taskKey)) continue;

                TaskRuntime runtime = zkService.getData(tasksPath + "/" + taskId, TaskRuntime.class);
                if (runtime == null) continue;

                if (runtime.getStatus() != TaskStatus.RUNNING &&
                    runtime.getStatus() != TaskStatus.RETRYING) {
                    continue;
                }

                String runningPath = ZkPaths.runningTaskPath(taskKey);
                if (zkService.exists(runningPath)) {
                    String assignedWorker = zkService.getData(runningPath, String.class);
                    if (!workerId.equals(assignedWorker)) {
                        continue;
                    }
                }

                if (acquireTask(taskKey)) {
                    executeTask(dag, dagId, taskId, runtime);
                }
            }
        }
    }

    private boolean acquireTask(String taskKey) {
        String runningPath = ZkPaths.runningTaskPath(taskKey);
        boolean created = zkService.createEphemeralIfAbsent(runningPath, workerId);
        if (created) {
            executingTasks.add(taskKey);
            logger.info("Acquired task: {}", taskKey);
            return true;
        }

        String currentWorker = zkService.getData(runningPath, String.class);
        if (workerId.equals(currentWorker)) {
            executingTasks.add(taskKey);
            return true;
        }

        return false;
    }

    private void executeTask(DAGDefinition dag, String dagId, String taskId, TaskRuntime runtime) {
        TaskNode originalTaskNode = dag.getTasks().get(taskId);
        if (originalTaskNode == null) {
            logger.error("Task node not found in DAG: {}/{}", dagId, taskId);
            return;
        }

        ParameterResolver resolver = new ParameterResolver(resultStore, dagId);
        Map<String, Object> resolvedParams = resolver.resolveParams(originalTaskNode.getParams());

        TaskNode taskNode = new TaskNode();
        taskNode.setId(originalTaskNode.getId());
        taskNode.setName(originalTaskNode.getName());
        taskNode.setType(originalTaskNode.getType());
        taskNode.setTimeoutMs(originalTaskNode.getTimeoutMs());
        taskNode.setMaxRetries(originalTaskNode.getMaxRetries());
        taskNode.setParams(resolvedParams);

        String taskKey = dagId + "_" + taskId;
        logger.info("Executing task {}/{} (retry={})", dagId, taskId, runtime.getRetryCount());

        runtime.setStatus(TaskStatus.RUNNING);
        runtime.setWorkerId(workerId);
        runtime.setStartTime(System.currentTimeMillis());
        zkService.setData(ZkPaths.dagTaskPath(dagId, taskId), runtime);

        Thread taskThread = new Thread(() -> {
            try {
                ExecutionResult result = taskExecutor.executeWithTimeoutAndRetry(taskNode, runtime);
                runtime.setEndTime(System.currentTimeMillis());

                if (result.isSuccess()) {
                    runtime.setStatus(TaskStatus.SUCCESS);
                    runtime.setResult(result.getOutput());
                    runtime.setErrorMessage(null);
                    logger.info("Task {}/{} completed successfully", dagId, taskId);

                    resultStore.storeResult(dagId, taskId, result.getOutput());
                } else {
                    int maxRetries = Math.min(taskNode.getMaxRetries(), 3);
                    if (runtime.getRetryCount() >= maxRetries) {
                        runtime.setStatus(TaskStatus.FAILED);
                        runtime.setErrorMessage(result.getMessage());
                        logger.error("Task {}/{} permanently failed: {}", dagId, taskId, result.getMessage());
                    } else {
                        runtime.setStatus(TaskStatus.RETRYING);
                        runtime.setErrorMessage(result.getMessage());
                        runtime.incrementRetryCount();
                        logger.warn("Task {}/{} failed, will retry (count={}/{}): {}",
                                dagId, taskId, runtime.getRetryCount(), maxRetries, result.getMessage());
                    }
                }

                zkService.setData(ZkPaths.dagTaskPath(dagId, taskId), runtime);

            } catch (Exception e) {
                runtime.setEndTime(System.currentTimeMillis());
                int maxRetries = Math.min(taskNode.getMaxRetries(), 3);
                if (runtime.getRetryCount() >= maxRetries) {
                    runtime.setStatus(TaskStatus.FAILED);
                    runtime.setErrorMessage("Unexpected error: " + e.getMessage());
                    logger.error("Task {}/{} failed with unexpected error: {}", dagId, taskId, e.getMessage());
                } else {
                    runtime.setStatus(TaskStatus.RETRYING);
                    runtime.setErrorMessage("Unexpected error: " + e.getMessage());
                    runtime.incrementRetryCount();
                }
                zkService.setData(ZkPaths.dagTaskPath(dagId, taskId), runtime);

            } finally {
                executingTasks.remove(taskKey);
                zkService.delete(ZkPaths.runningTaskPath(taskKey));
                logger.info("Released task: {}", taskKey);
            }
        });
        taskThread.setName("worker-task-" + taskKey);
        taskThread.setDaemon(true);
        taskThread.start();
    }

    private void checkExecutionTimeouts() {
        long now = System.currentTimeMillis();
        Set<String> toRemove = new HashSet<>();

        for (String taskKey : executingTasks) {
            String[] parts = taskKey.split("_", 2);
            if (parts.length != 2) {
                toRemove.add(taskKey);
                continue;
            }
            String dagId = parts[0];
            String taskId = parts[1];

            TaskRuntime runtime = zkService.getData(ZkPaths.dagTaskPath(dagId, taskId), TaskRuntime.class);
            if (runtime == null) {
                toRemove.add(taskKey);
                continue;
            }

            DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
            if (dag == null) {
                toRemove.add(taskKey);
                continue;
            }

            TaskNode taskNode = dag.getTasks().get(taskId);
            long timeout = taskNode != null && taskNode.getTimeoutMs() > 0 ? taskNode.getTimeoutMs() : 5 * 60 * 1000L;

            if (runtime.getStatus() == TaskStatus.RUNNING &&
                runtime.getStartTime() > 0 &&
                (now - runtime.getStartTime()) > timeout) {
                logger.warn("Task {}/{} exceeded timeout, marking for retry", dagId, taskId);
            }
        }

        executingTasks.removeAll(toRemove);
    }

    public String getWorkerId() {
        return workerId;
    }

    public void shutdown() {
        scheduler.shutdown();
        try {
            if (!scheduler.awaitTermination(5, TimeUnit.SECONDS)) {
                scheduler.shutdownNow();
            }
        } catch (InterruptedException e) {
            scheduler.shutdownNow();
            Thread.currentThread().interrupt();
        }
        taskExecutor.shutdown();
        zkService.close();
    }
}
