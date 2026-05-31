package com.orchestrator.zookeeper;

import com.orchestrator.model.DAGDefinition;
import com.orchestrator.model.TaskNode;
import com.orchestrator.model.TaskRuntime;
import com.orchestrator.model.TaskStatus;
import com.orchestrator.result.ParameterResolver;
import com.orchestrator.result.ResultStore;
import org.apache.zookeeper.Watcher;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;

public class MasterScheduler {

    private static final Logger logger = LoggerFactory.getLogger(MasterScheduler.class);

    private final ZkService zkService;
    private final ResultStore resultStore;
    private final String masterId;
    private boolean isLeader = false;
    private String currentMasterPath;
    private final ScheduledExecutorService scheduler;

    public MasterScheduler(ZkService zkService) {
        this.zkService = zkService;
        this.resultStore = new ResultStore(zkService);
        this.masterId = UUID.randomUUID().toString().replace("-", "").substring(0, 8);
        this.scheduler = Executors.newScheduledThreadPool(2);
    }

    public void start() {
        tryElection();
        setupRunningTaskWatch();
        scheduler.scheduleAtFixedRate(this::scheduleReadyTasks, 5, 10, TimeUnit.SECONDS);
        scheduler.scheduleAtFixedRate(this::checkCrashedWorkers, 5, 5, TimeUnit.SECONDS);
        logger.info("Master scheduler started, masterId={}", masterId);
    }

    private void tryElection() {
        try {
            String path = zkService.createEphemeralSequential(ZkPaths.MASTERS + "/master-", masterId);
            currentMasterPath = path;
            checkLeadership();
            zkService.getChildren(ZkPaths.MASTERS, event -> {
                if (event.getType() == Watcher.Event.EventType.NodeChildrenChanged) {
                    checkLeadership();
                }
            });
            logger.info("Master election candidate registered: {}", path);
        } catch (Exception e) {
            logger.error("Error in master election: {}", e.getMessage());
        }
    }

    private void checkLeadership() {
        List<String> children = zkService.getChildren(ZkPaths.MASTERS);
        if (children.isEmpty()) return;

        Collections.sort(children);
        String smallest = children.get(0);
        String smallestPath = ZkPaths.MASTERS + "/" + smallest;

        if (smallestPath.equals(currentMasterPath)) {
            if (!isLeader) {
                isLeader = true;
                logger.info("This node is now the LEADER: {}", masterId);
            }
        } else {
            if (isLeader) {
                isLeader = false;
                logger.info("This node lost leadership");
            }
            zkService.addWatch(smallestPath, event -> {
                if (event.getType() == Watcher.Event.EventType.NodeDeleted) {
                    checkLeadership();
                }
            });
        }
    }

    private void setupRunningTaskWatch() {
        zkService.getChildren(ZkPaths.RUNNING_TASKS, event -> {
            if (event.getType() == Watcher.Event.EventType.NodeChildrenChanged) {
                logger.info("Running tasks children changed, checking for crashed workers...");
            }
        });
    }

    public void scheduleReadyTasks() {
        if (!isLeader) return;

        List<String> dagIds = zkService.getChildren(ZkPaths.DAGS);
        for (String dagId : dagIds) {
            try {
                scheduleDagTasks(dagId);
            } catch (Exception e) {
                logger.error("Error scheduling tasks for DAG {}: {}", dagId, e.getMessage());
            }
        }
    }

    private void scheduleDagTasks(String dagId) {
        DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
        if (dag == null) return;

        Set<String> completedTasks = new HashSet<>();
        Map<String, TaskRuntime> runtimeMap = new HashMap<>();
        String tasksPath = ZkPaths.dagTasksPath(dagId);

        if (!zkService.exists(tasksPath)) return;

        for (String taskId : zkService.getChildren(tasksPath)) {
            TaskRuntime runtime = zkService.getData(tasksPath + "/" + taskId, TaskRuntime.class);
            if (runtime != null) {
                runtimeMap.put(taskId, runtime);
                if (runtime.getStatus() == TaskStatus.SUCCESS) {
                    completedTasks.add(taskId);
                }
            }
        }

        boolean allTasksDone = runtimeMap.values().stream()
                .allMatch(r -> r.getStatus() == TaskStatus.SUCCESS || r.getStatus() == TaskStatus.FAILED);
        if (allTasksDone && !runtimeMap.isEmpty()) {
            logger.info("DAG {} has all tasks completed, total={}, success={}, failed={}",
                    dagId,
                    runtimeMap.size(),
                    runtimeMap.values().stream().filter(r -> r.getStatus() == TaskStatus.SUCCESS).count(),
                    runtimeMap.values().stream().filter(r -> r.getStatus() == TaskStatus.FAILED).count());
            return;
        }

        for (String taskId : dag.getTasks().keySet()) {
            if (completedTasks.contains(taskId)) continue;

            TaskRuntime runtime = runtimeMap.get(taskId);
            if (runtime == null) {
                runtime = new TaskRuntime(dagId, taskId);
                zkService.createPersistent(tasksPath + "/" + taskId, runtime);
                runtimeMap.put(taskId, runtime);
            }

            TaskStatus status = runtime.getStatus();
            if (status == TaskStatus.RUNNING || status == TaskStatus.SUCCESS || status == TaskStatus.FAILED) {
                continue;
            }

            TaskNode taskNode = dag.getTasks().get(taskId);
            List<String> deps = dag.getDependencies(taskId);
            boolean depsSatisfied = deps.stream().allMatch(completedTasks::contains);
            if (!depsSatisfied) continue;

            boolean upstreamResultsReady = checkUpstreamResults(dagId, taskNode, deps);
            if (!upstreamResultsReady) {
                logger.debug("Upstream results not ready for task {}/{}, deferring", dagId, taskId);
                continue;
            }

            if (status == TaskStatus.PENDING || status == TaskStatus.RETRYING) {
                runtime.setStatus(TaskStatus.RUNNING);
                runtime.setStartTime(System.currentTimeMillis());
                zkService.setData(tasksPath + "/" + taskId, runtime);
                logger.info("Task {}/{} scheduled for execution (retry={})", dagId, taskId, runtime.getRetryCount());
            }
        }
    }

    private boolean checkUpstreamResults(String dagId, TaskNode taskNode, List<String> deps) {
        ParameterResolver resolver = new ParameterResolver(resultStore, dagId);
        Set<String> referencedUpstreams = resolver.extractUpstreamTaskIds(taskNode.getParams());

        Set<String> allUpstreams = new HashSet<>(deps);
        allUpstreams.addAll(referencedUpstreams);

        for (String upstreamId : allUpstreams) {
            if (!resultStore.hasResult(dagId, upstreamId)) {
                logger.debug("Upstream result missing for {}/{} (upstream={})", dagId, taskNode.getId(), upstreamId);
                return false;
            }
        }
        return true;
    }

    public void checkCrashedWorkers() {
        if (!isLeader) return;

        List<String> runningTaskNodes = zkService.getChildren(ZkPaths.RUNNING_TASKS);
        Set<String> runningTaskIds = new HashSet<>(runningTaskNodes);

        List<String> dagIds = zkService.getChildren(ZkPaths.DAGS);
        for (String dagId : dagIds) {
            String tasksPath = ZkPaths.dagTasksPath(dagId);
            if (!zkService.exists(tasksPath)) continue;

            for (String taskId : zkService.getChildren(tasksPath)) {
                TaskRuntime runtime = zkService.getData(tasksPath + "/" + taskId, TaskRuntime.class);
                if (runtime == null || runtime.getStatus() != TaskStatus.RUNNING) continue;

                String runtimeNodeId = dagId + "_" + taskId;
                if (!runningTaskIds.contains(runtimeNodeId)) {
                    handleWorkerCrash(dagId, taskId, runtime);
                }
            }
        }
    }

    private void handleWorkerCrash(String dagId, String taskId, TaskRuntime runtime) {
        TaskNode taskNode = getTaskNode(dagId, taskId);
        int maxRetries = taskNode != null ? taskNode.getMaxRetries() : 3;

        logger.warn("Detected worker crash for task {}/{} (retryCount={}, maxRetries={}), rescheduling...",
                dagId, taskId, runtime.getRetryCount(), maxRetries);

        runtime.incrementRetryCount();
        if (runtime.getRetryCount() >= maxRetries) {
            runtime.setStatus(TaskStatus.FAILED);
            runtime.setErrorMessage("Task failed after " + maxRetries + " retries due to worker crashes");
            runtime.setEndTime(System.currentTimeMillis());
            logger.error("Task {}/{} permanently failed after {} retries due to worker crashes",
                    dagId, taskId, maxRetries);
        } else {
            runtime.setStatus(TaskStatus.RETRYING);
            runtime.setErrorMessage("Worker crashed, retry " + runtime.getRetryCount() + " of " + maxRetries);
            runtime.setEndTime(System.currentTimeMillis());
        }

        zkService.setData(ZkPaths.dagTaskPath(dagId, taskId), runtime);
    }

    private TaskNode getTaskNode(String dagId, String taskId) {
        DAGDefinition dag = zkService.getData(ZkPaths.dagPath(dagId), DAGDefinition.class);
        if (dag != null) {
            return dag.getTasks().get(taskId);
        }
        return null;
    }

    public boolean isLeader() {
        return isLeader;
    }

    public String getMasterId() {
        return masterId;
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
        zkService.close();
    }
}
