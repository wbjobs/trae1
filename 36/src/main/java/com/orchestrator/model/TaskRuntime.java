package com.orchestrator.model;

public class TaskRuntime {

    private String dagId;
    private String taskId;
    private TaskStatus status;
    private int retryCount;
    private long startTime;
    private long endTime;
    private String workerId;
    private String result;
    private String errorMessage;

    public TaskRuntime() {}

    public TaskRuntime(String dagId, String taskId) {
        this.dagId = dagId;
        this.taskId = taskId;
        this.status = TaskStatus.PENDING;
        this.retryCount = 0;
    }

    public String getDagId() { return dagId; }
    public void setDagId(String dagId) { this.dagId = dagId; }

    public String getTaskId() { return taskId; }
    public void setTaskId(String taskId) { this.taskId = taskId; }

    public TaskStatus getStatus() { return status; }
    public void setStatus(TaskStatus status) { this.status = status; }

    public int getRetryCount() { return retryCount; }
    public void setRetryCount(int retryCount) { this.retryCount = retryCount; }
    public void incrementRetryCount() { this.retryCount++; }

    public long getStartTime() { return startTime; }
    public void setStartTime(long startTime) { this.startTime = startTime; }

    public long getEndTime() { return endTime; }
    public void setEndTime(long endTime) { this.endTime = endTime; }

    public String getWorkerId() { return workerId; }
    public void setWorkerId(String workerId) { this.workerId = workerId; }

    public String getResult() { return result; }
    public void setResult(String result) { this.result = result; }

    public String getErrorMessage() { return errorMessage; }
    public void setErrorMessage(String errorMessage) { this.errorMessage = errorMessage; }
}
