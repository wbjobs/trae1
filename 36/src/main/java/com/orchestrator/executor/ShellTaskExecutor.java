package com.orchestrator.executor;

import com.orchestrator.model.TaskNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public class ShellTaskExecutor {

    private static final Logger logger = LoggerFactory.getLogger(ShellTaskExecutor.class);

    public ExecutionResult execute(TaskNode taskNode) {
        Map<String, Object> params = taskNode.getParams();
        String command = (String) params.get("command");

        if (command == null || command.isEmpty()) {
            return ExecutionResult.failure("Shell command is empty");
        }

        String os = System.getProperty("os.name").toLowerCase();
        ProcessBuilder pb;
        if (os.contains("win")) {
            pb = new ProcessBuilder("cmd", "/c", command);
        } else {
            pb = new ProcessBuilder("/bin/sh", "-c", command);
        }
        pb.redirectErrorStream(true);

        if (params.containsKey("workingDir")) {
            pb.directory(new java.io.File((String) params.get("workingDir")));
        }

        StringBuilder output = new StringBuilder();
        try {
            Process process = pb.start();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    output.append(line).append("\n");
                    logger.debug("[SHELL] {}", line);
                }
            }

            long timeoutMs = taskNode.getTimeoutMs() > 0 ? taskNode.getTimeoutMs() : 5 * 60 * 1000L;
            boolean finished = process.waitFor(timeoutMs, TimeUnit.MILLISECONDS);

            if (!finished) {
                process.destroyForcibly();
                return ExecutionResult.failure("Shell command timed out after " + timeoutMs + "ms");
            }

            int exitCode = process.exitValue();
            if (exitCode == 0) {
                return ExecutionResult.success(output.toString());
            } else {
                return ExecutionResult.failure("Shell command exited with code " + exitCode + ", output: " + output);
            }
        } catch (Exception e) {
            logger.error("Shell execution error for task {}: {}", taskNode.getId(), e.getMessage());
            return ExecutionResult.failure("Shell execution error: " + e.getMessage());
        }
    }

    public void shutdown() {}
}
