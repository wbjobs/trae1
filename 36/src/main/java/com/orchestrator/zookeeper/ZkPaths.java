package com.orchestrator.zookeeper;

public class ZkPaths {

    public static final String ROOT = "/orchestrator";
    public static final String DAGS = ROOT + "/dags";
    public static final String MASTERS = ROOT + "/masters";
    public static final String WORKERS = ROOT + "/workers";
    public static final String RUNNING_TASKS = ROOT + "/running-tasks";
    public static final String RESULTS = ROOT + "/results";

    public static String dagPath(String dagId) {
        return DAGS + "/" + dagId;
    }

    public static String dagTasksPath(String dagId) {
        return dagPath(dagId) + "/tasks";
    }

    public static String dagTaskPath(String dagId, String taskId) {
        return dagTasksPath(dagId) + "/" + taskId;
    }

    public static String runningTaskPath(String taskRuntimeId) {
        return RUNNING_TASKS + "/" + taskRuntimeId;
    }

    public static String workerPath(String workerId) {
        return WORKERS + "/" + workerId;
    }

    public static String masterPath(String masterId) {
        return MASTERS + "/" + masterId;
    }

    public static String dagResultsPath(String dagId) {
        return RESULTS + "/" + dagId;
    }

    public static String taskResultPath(String dagId, String taskId) {
        return dagResultsPath(dagId) + "/" + taskId;
    }
}
