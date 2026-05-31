package com.orchestrator.executor;

public class ExecutionResult {

    private boolean success;
    private String message;
    private String output;

    private ExecutionResult(boolean success, String message, String output) {
        this.success = success;
        this.message = message;
        this.output = output;
    }

    public static ExecutionResult success(String output) {
        return new ExecutionResult(true, "success", output);
    }

    public static ExecutionResult success(String message, String output) {
        return new ExecutionResult(true, message, output);
    }

    public static ExecutionResult failure(String message) {
        return new ExecutionResult(false, message, null);
    }

    public boolean isSuccess() { return success; }
    public String getMessage() { return message; }
    public String getOutput() { return output; }
}
