package com.orchestrator.executor;

import com.orchestrator.model.TaskNode;
import com.orchestrator.model.TaskRuntime;
import com.orchestrator.model.TaskStatus;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.concurrent.*;

public class TaskExecutor {

    private static final Logger logger = LoggerFactory.getLogger(TaskExecutor.class);
    private static final long DEFAULT_TIMEOUT_MS = 5 * 60 * 1000L;
    private static final int MAX_GLOBAL_RETRIES = 3;

    private final ShellTaskExecutor shellExecutor;
    private final HttpTaskExecutor httpExecutor;
    private final SqlTaskExecutor sqlExecutor;

    public TaskExecutor() {
        this.shellExecutor = new ShellTaskExecutor();
        this.httpExecutor = new HttpTaskExecutor();
        this.sqlExecutor = new SqlTaskExecutor();
    }

    public ExecutionResult executeWithTimeoutAndRetry(TaskNode taskNode, TaskRuntime runtime) {
        long timeoutMs = taskNode.getTimeoutMs() > 0 ? taskNode.getTimeoutMs() : DEFAULT_TIMEOUT_MS;
        int maxRetries = Math.min(taskNode.getMaxRetries(), MAX_GLOBAL_RETRIES);
        int attempt = runtime.getRetryCount() + 1;

        while (attempt <= maxRetries) {
            logger.info("Executing task {}: attempt {}/{}", taskNode.getId(), attempt, maxRetries);

            ExecutorService executor = Executors.newSingleThreadExecutor();
            Future<ExecutionResult> future = executor.submit(() -> executeInternal(taskNode));

            try {
                ExecutionResult result = future.get(timeoutMs, TimeUnit.MILLISECONDS);
                executor.shutdown();

                if (result.isSuccess()) {
                    return result;
                }

                logger.warn("Task {} failed on attempt {}/{}: {}", taskNode.getId(), attempt, maxRetries, result.getMessage());

                if (attempt >= maxRetries) {
                    return result;
                }

                runtime.incrementRetryCount();
                runtime.setStatus(TaskStatus.RETRYING);
                runtime.setErrorMessage("Attempt " + attempt + " failed: " + result.getMessage());
                attempt++;

                long backoff = 1000L * (1L << (attempt - 1));
                Thread.sleep(Math.min(backoff, 30000));

            } catch (TimeoutException e) {
                future.cancel(true);
                executor.shutdownNow();
                logger.error("Task {} timed out after {} ms", taskNode.getId(), timeoutMs);

                if (attempt >= maxRetries) {
                    return ExecutionResult.failure("Task timed out after " + timeoutMs + "ms on attempt " + attempt);
                }

                runtime.incrementRetryCount();
                runtime.setStatus(TaskStatus.RETRYING);
                runtime.setErrorMessage("Timeout on attempt " + attempt);
                attempt++;

                try { Thread.sleep(2000); } catch (InterruptedException ie) { Thread.currentThread().interrupt(); }

            } catch (InterruptedException e) {
                future.cancel(true);
                executor.shutdownNow();
                Thread.currentThread().interrupt();
                return ExecutionResult.failure("Task execution interrupted");

            } catch (ExecutionException e) {
                executor.shutdown();
                Throwable cause = e.getCause() != null ? e.getCause() : e;
                logger.error("Task {} execution error on attempt {}: {}", taskNode.getId(), attempt, cause.getMessage());

                if (attempt >= maxRetries) {
                    return ExecutionResult.failure("Task failed after " + attempt + " attempts: " + cause.getMessage());
                }

                runtime.incrementRetryCount();
                runtime.setStatus(TaskStatus.RETRYING);
                runtime.setErrorMessage("Attempt " + attempt + " error: " + cause.getMessage());
                attempt++;

                try { Thread.sleep(2000); } catch (InterruptedException ie) { Thread.currentThread().interrupt(); }
            }
        }

        return ExecutionResult.failure("Task failed after " + maxRetries + " retries");
    }

    private ExecutionResult executeInternal(TaskNode taskNode) {
        switch (taskNode.getType()) {
            case SHELL:
                return shellExecutor.execute(taskNode);
            case HTTP:
                return httpExecutor.execute(taskNode);
            case SQL:
                return sqlExecutor.execute(taskNode);
            default:
                return ExecutionResult.failure("Unknown task type: " + taskNode.getType());
        }
    }

    public void shutdown() {
        shellExecutor.shutdown();
        httpExecutor.shutdown();
        sqlExecutor.shutdown();
    }
}
