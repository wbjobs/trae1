package com.orchestrator.result;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.orchestrator.zookeeper.ZkPaths;
import com.orchestrator.zookeeper.ZkService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Map;

public class ResultStore {

    private static final Logger logger = LoggerFactory.getLogger(ResultStore.class);
    private static final int MAX_RESULT_BYTES = 1024 * 1024; // 1MB

    private final ZkService zkService;
    private final ObjectMapper objectMapper;

    public ResultStore(ZkService zkService) {
        this.zkService = zkService;
        this.objectMapper = zkService.getObjectMapper();
    }

    public void storeResult(String dagId, String taskId, String jsonResult) {
        if (jsonResult == null || jsonResult.isEmpty()) {
            logger.debug("Empty result for {}/{}, skipping storage", dagId, taskId);
            return;
        }

        byte[] bytes = jsonResult.getBytes();
        if (bytes.length > MAX_RESULT_BYTES) {
            logger.warn("Result for {}/{} exceeds 1MB limit ({} bytes), truncating", dagId, taskId, bytes.length);
            jsonResult = new String(bytes, 0, MAX_RESULT_BYTES);
            bytes = jsonResult.getBytes();
        }

        String dagResultsPath = ZkPaths.dagResultsPath(dagId);
        zkService.ensurePath(dagResultsPath);

        String resultPath = ZkPaths.taskResultPath(dagId, taskId);
        zkService.createPersistent(resultPath, jsonResult);

        logger.info("Stored result for {}/{} ({} bytes)", dagId, taskId, bytes.length);
    }

    public Map<String, Object> getResult(String dagId, String taskId) {
        String resultPath = ZkPaths.taskResultPath(dagId, taskId);
        String json = zkService.getData(resultPath, String.class);
        if (json == null || json.isEmpty()) {
            return null;
        }
        try {
            return objectMapper.readValue(json, Map.class);
        } catch (JsonProcessingException e) {
            logger.error("Failed to parse result for {}/{}: {}", dagId, taskId, e.getMessage());
            return null;
        }
    }

    public String getResultRaw(String dagId, String taskId) {
        String resultPath = ZkPaths.taskResultPath(dagId, taskId);
        return zkService.getData(resultPath, String.class);
    }

    public boolean hasResult(String dagId, String taskId) {
        String resultPath = ZkPaths.taskResultPath(dagId, taskId);
        return zkService.exists(resultPath);
    }

    public Object getField(String dagId, String taskId, String fieldPath) {
        Map<String, Object> result = getResult(dagId, taskId);
        if (result == null) return null;

        String[] parts = fieldPath.split("\\.");
        Object current = result;
        for (String part : parts) {
            if (current instanceof Map) {
                current = ((Map<?, ?>) current).get(part);
                if (current == null) return null;
            } else {
                return null;
            }
        }
        return current;
    }

    public void deleteResult(String dagId, String taskId) {
        String resultPath = ZkPaths.taskResultPath(dagId, taskId);
        if (zkService.exists(resultPath)) {
            zkService.delete(resultPath);
        }
    }
}
