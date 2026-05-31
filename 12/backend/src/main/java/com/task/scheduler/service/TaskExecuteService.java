package com.task.scheduler.service;

import com.task.scheduler.entity.ServerNode;
import com.task.scheduler.entity.TaskConfig;
import com.task.scheduler.entity.TaskLog;
import com.task.scheduler.repository.ServerNodeRepository;
import com.task.scheduler.repository.TaskLogRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.time.LocalDateTime;
import java.util.List;

@Service
public class TaskExecuteService {

    private static final Logger logger = LoggerFactory.getLogger(TaskExecuteService.class);

    @Autowired
    private ServerNodeRepository serverNodeRepository;

    @Autowired
    private TaskLogRepository taskLogRepository;

    @Value("${task.scheduler.retry.max-attempts:3}")
    private int maxRetryAttempts;

    @Value("${task.scheduler.retry.delay-ms:5000}")
    private long retryDelayMs;

    public TaskLog executeTask(TaskConfig taskConfig) {
        TaskLog taskLog = createTaskLog(taskConfig);
        taskLog.setExecuteStatus(1);
        taskLogRepository.save(taskLog);

        try {
            String result = executeOnServer(taskConfig);
            taskLog.setExecuteStatus(2);
            taskLog.setExecuteResult(result);
            taskConfig.setLastExecuteResult("SUCCESS");
            logger.info("Task {} executed successfully on server {}", taskConfig.getTaskName(), taskConfig.getTargetServer());
        } catch (Exception e) {
            logger.error("Task {} execution failed: {}", taskConfig.getTaskName(), e.getMessage());
            taskLog.setExecuteStatus(0);
            taskLog.setErrorMessage(e.getMessage());
            taskConfig.setLastExecuteResult("FAILED");

            if (taskLog.getRetryAttempts() < taskConfig.getRetryCount()) {
                retryTask(taskConfig, taskLog);
            }
        }

        taskLog.setEndTime(LocalDateTime.now());
        taskLog.setDuration(java.time.Duration.between(taskLog.getStartTime(), taskLog.getEndTime()).getSeconds());

        taskConfig.setLastExecuteTime(LocalDateTime.now());

        return taskLog;
    }

    private TaskLog createTaskLog(TaskConfig taskConfig) {
        TaskLog taskLog = new TaskLog();
        taskLog.setTaskId(taskConfig.getId());
        taskLog.setTaskName(taskConfig.getTaskName());
        taskLog.setServerName(taskConfig.getTargetServer());
        taskLog.setStartTime(LocalDateTime.now());
        taskLog.setExecuteStatus(1);
        taskLog.setRetryAttempts(0);
        return taskLog;
    }

    private void retryTask(TaskConfig taskConfig, TaskLog taskLog) {
        try {
            Thread.sleep(taskConfig.getRetryInterval() * 1000L);
            taskLog.setRetryAttempts(taskLog.getRetryAttempts() + 1);
            logger.info("Retrying task {}, attempt {}", taskConfig.getTaskName(), taskLog.getRetryAttempts());
            executeTask(taskConfig);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }

    public String executeOnServer(TaskConfig taskConfig) throws Exception {
        ServerNode serverNode = serverNodeRepository.findByServerNameAndDeleted(taskConfig.getTargetServer(), 0);
        if (serverNode == null) {
            throw new RuntimeException("Target server not found: " + taskConfig.getTargetServer());
        }

        if (serverNode.getStatus() != 1) {
            throw new RuntimeException("Target server is not online: " + taskConfig.getTargetServer());
        }

        return executeCommand(serverNode, taskConfig.getExecuteCommand(), taskConfig.getTimeout());
    }

    private String executeCommand(ServerNode serverNode, String command, int timeout) throws Exception {
        logger.info("Executing command on {} ({}): {}", serverNode.getServerName(), serverNode.getIpAddress(), command);

        StringBuilder result = new StringBuilder();
        Process process = null;

        try {
            ProcessBuilder processBuilder = new ProcessBuilder();
            if ("Windows".equalsIgnoreCase(serverNode.getOsType())) {
                processBuilder.command("cmd", "/c", command);
            } else {
                processBuilder.command("ssh",
                        "-o", "StrictHostKeyChecking=no",
                        "-o", "ConnectTimeout=10",
                        "-p", String.valueOf(serverNode.getPort()),
                        serverNode.getUsername() + "@" + serverNode.getIpAddress(),
                        command);
            }

            processBuilder.redirectErrorStream(true);
            process = processBuilder.start();

            BufferedReader reader = new BufferedReader(new InputStreamReader(process.getInputStream()));
            String line;
            while ((line = reader.readLine()) != null) {
                result.append(line).append("\n");
            }

            boolean finished = process.waitFor(timeout, java.util.concurrent.TimeUnit.SECONDS);
            if (!finished) {
                process.destroyForcibly();
                throw new RuntimeException("Command execution timed out after " + timeout + " seconds");
            }

            int exitCode = process.exitValue();
            if (exitCode != 0) {
                throw new RuntimeException("Command execution failed with exit code: " + exitCode);
            }

            logger.info("Command executed successfully on {}", serverNode.getServerName());
            return result.toString();

        } catch (Exception e) {
            logger.error("Command execution failed on {}: {}", serverNode.getServerName(), e.getMessage());
            throw e;
        } finally {
            if (process != null) {
                process.destroy();
            }
        }
    }

    public boolean testConnection(ServerNode serverNode) {
        try {
            String testCommand = "echo 'Connection test successful'";
            String result = executeCommand(serverNode, testCommand, 10);
            return result.contains("Connection test successful");
        } catch (Exception e) {
            logger.error("Connection test failed for server {}: {}", serverNode.getServerName(), e.getMessage());
            return false;
        }
    }

    public void distributeTaskToServers(TaskConfig taskConfig, List<String> serverNames) {
        for (String serverName : serverNames) {
            TaskConfig distributedTask = new TaskConfig();
            distributedTask.setTaskName(taskConfig.getTaskName() + "_" + serverName);
            distributedTask.setTaskGroup(taskConfig.getTaskGroup());
            distributedTask.setCronExpression(taskConfig.getCronExpression());
            distributedTask.setTaskType(taskConfig.getTaskType());
            distributedTask.setTaskParams(taskConfig.getTaskParams());
            distributedTask.setTargetServer(serverName);
            distributedTask.setExecuteCommand(taskConfig.getExecuteCommand());
            distributedTask.setDescription(taskConfig.getDescription() + " - Distributed to " + serverName);
            distributedTask.setRetryCount(taskConfig.getRetryCount());
            distributedTask.setRetryInterval(taskConfig.getRetryInterval());
            distributedTask.setTimeout(taskConfig.getTimeout());
            distributedTask.setStatus(0);

            executeTask(distributedTask);
        }
    }

    public String executeHttpTask(String url, String method, String params, int timeout) throws Exception {
        logger.info("Executing HTTP task: {} {}", method, url);

        org.apache.http.client.methods.HttpRequestBase request;
        if ("POST".equalsIgnoreCase(method)) {
            org.apache.http.client.methods.HttpPost post = new org.apache.http.client.methods.HttpPost(url);
            if (params != null && !params.isEmpty()) {
                post.setEntity(new org.apache.http.entity.StringEntity(params, "UTF-8"));
                post.setHeader("Content-Type", "application/json");
            }
            request = post;
        } else {
            String fullUrl = url;
            if (params != null && !params.isEmpty()) {
                fullUrl += (url.contains("?") ? "&" : "?") + params;
            }
            request = new org.apache.http.client.methods.HttpGet(fullUrl);
        }

        org.apache.http.impl.client.CloseableHttpClient httpClient = org.apache.http.impl.client.HttpClients.createDefault();
        try {
            org.apache.http.client.config.RequestConfig config = org.apache.http.client.config.RequestConfig.custom()
                    .setConnectTimeout(timeout * 1000)
                    .setSocketTimeout(timeout * 1000)
                    .build();
            request.setConfig(config);

            org.apache.http.HttpResponse response = httpClient.execute(request);
            org.apache.http.HttpEntity entity = response.getEntity();

            if (entity != null) {
                return org.apache.http.util.EntityUtils.toString(entity, "UTF-8");
            }
            return "";
        } finally {
            httpClient.close();
        }
    }
}
