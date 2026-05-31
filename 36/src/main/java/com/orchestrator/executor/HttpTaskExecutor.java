package com.orchestrator.executor;

import com.orchestrator.model.TaskNode;
import org.apache.hc.client5.http.classic.methods.*;
import org.apache.hc.client5.http.impl.classic.*;
import org.apache.hc.core5.http.*;
import org.apache.hc.core5.http.io.entity.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Map;

public class HttpTaskExecutor {

    private static final Logger logger = LoggerFactory.getLogger(HttpTaskExecutor.class);
    private final CloseableHttpClient httpClient;

    public HttpTaskExecutor() {
        this.httpClient = HttpClients.createDefault();
    }

    public ExecutionResult execute(TaskNode taskNode) {
        Map<String, Object> params = taskNode.getParams();
        String url = (String) params.get("url");
        String method = params.getOrDefault("method", "GET").toString().toUpperCase();
        String body = (String) params.get("body");
        @SuppressWarnings("unchecked")
        Map<String, String> headers = (Map<String, String>) params.get("headers");

        if (url == null || url.isEmpty()) {
            return ExecutionResult.failure("HTTP URL is empty");
        }

        try {
            ClassicHttpRequest request = createRequest(method, url);

            if (headers != null) {
                for (Map.Entry<String, String> entry : headers.entrySet()) {
                    request.addHeader(entry.getKey(), entry.getValue());
                }
            }

            if (body != null && !body.isEmpty() && request instanceof ClassicHttpRequest) {
                request.setEntity(new StringEntity(body, ContentType.APPLICATION_JSON));
            }

            StringBuilder output = new StringBuilder();
            CloseableHttpResponse response = httpClient.execute(request);
            try {
                int statusCode = response.getCode();
                HttpEntity entity = response.getEntity();

                if (entity != null) {
                    try (BufferedReader reader = new BufferedReader(
                            new InputStreamReader(entity.getContent()))) {
                        String line;
                        while ((line = reader.readLine()) != null) {
                            output.append(line).append("\n");
                        }
                    }
                }

                if (statusCode >= 200 && statusCode < 300) {
                    return ExecutionResult.success(output.toString());
                } else {
                    return ExecutionResult.failure("HTTP " + statusCode + ": " + output);
                }
            } finally {
                response.close();
            }
        } catch (Exception e) {
            logger.error("HTTP execution error for task {}: {}", taskNode.getId(), e.getMessage());
            return ExecutionResult.failure("HTTP execution error: " + e.getMessage());
        }
    }

    private ClassicHttpRequest createRequest(String method, String url) {
        switch (method) {
            case "GET":     return new HttpGet(url);
            case "POST":    return new HttpPost(url);
            case "PUT":     return new HttpPut(url);
            case "DELETE":  return new HttpDelete(url);
            case "PATCH":   return new HttpPatch(url);
            case "HEAD":    return new HttpHead(url);
            case "OPTIONS": return new HttpOptions(url);
            default:        return new HttpGet(url);
        }
    }

    public void shutdown() {
        try {
            httpClient.close();
        } catch (Exception e) {
            logger.error("Error closing HTTP client: {}", e.getMessage());
        }
    }
}
