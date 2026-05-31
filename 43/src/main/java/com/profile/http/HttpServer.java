package com.profile.http;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.profile.config.AppConfig;
import com.profile.service.RedisSwitchManager;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static spark.Spark.*;

public class HttpServer {

    private static final Logger log = LoggerFactory.getLogger(HttpServer.class);

    private static final ObjectMapper MAPPER = new ObjectMapper();

    private final AppConfig config;
    private final RedisSwitchManager switchManager;

    public HttpServer(AppConfig config, RedisSwitchManager switchManager) {
        this.config = config;
        this.switchManager = switchManager;
    }

    public void start() {
        port(config.getHttpPort());
        threadPool(200, 20, 30000);

        before((req, res) -> {
            res.header("Content-Type", "application/json; charset=utf-8");
        });

        get("/user/:id/tags", (req, res) -> {
            String userId = req.params(":id");
            if (userId == null || userId.trim().isEmpty()) {
                res.status(400);
                return errorJson("userId is required");
            }
            long start = System.nanoTime();
            List<Map<String, Object>> tags = switchManager.getProfile(userId);
            long cost = (System.nanoTime() - start) / 1_000_000;
            res.header("X-Response-Time", cost + "ms");
            res.header("X-Data-Source", switchManager.getActiveSource());
            Map<String, Object> body = new HashMap<>();
            body.put("userId", userId);
            body.put("tags", tags);
            body.put("total", tags.size());
            body.put("costMs", cost);
            body.put("source", switchManager.getActiveSource());
            return MAPPER.writeValueAsString(body);
        });

        post("/admin/switch/new", (req, res) -> {
            boolean ok = switchManager.switchToNew();
            Map<String, Object> body = new HashMap<>();
            body.put("success", ok);
            body.put("activeSource", switchManager.getActiveSource());
            body.put("message", ok ? "Switched to NEW Redis" : "Switch failed or already active");
            return MAPPER.writeValueAsString(body);
        });

        post("/admin/switch/primary", (req, res) -> {
            boolean ok = switchManager.switchBack();
            Map<String, Object> body = new HashMap<>();
            body.put("success", ok);
            body.put("activeSource", switchManager.getActiveSource());
            body.put("message", ok ? "Switched back to PRIMARY Redis" : "Switch failed or already active");
            return MAPPER.writeValueAsString(body);
        });

        get("/admin/source", (req, res) -> {
            Map<String, Object> body = new HashMap<>();
            body.put("activeSource", switchManager.getActiveSource());
            body.put("useNewRedis", config.isUseNewRedis());
            return MAPPER.writeValueAsString(body);
        });

        get("/admin/verify/:id", (req, res) -> {
            String userId = req.params(":id");
            if (userId == null || userId.trim().isEmpty()) {
                res.status(400);
                return errorJson("userId is required");
            }
            Map<String, Object> comparison = switchManager.compareProfiles(userId);
            return MAPPER.writeValueAsString(comparison);
        });

        get("/health", (req, res) -> {
            Map<String, Object> body = new HashMap<>();
            body.put("status", "ok");
            body.put("source", switchManager.getActiveSource());
            return MAPPER.writeValueAsString(body);
        });

        exception(Exception.class, (e, req, res) -> {
            log.error("HTTP error", e);
            res.status(500);
            res.body(errorJson("internal error: " + e.getMessage()));
        });

        log.info("HTTP server started on port {}", config.getHttpPort());
    }

    public void stop() {
        spark.Spark.stop();
    }

    private String errorJson(String msg) throws Exception {
        Map<String, Object> body = new HashMap<>();
        body.put("error", msg);
        return MAPPER.writeValueAsString(body);
    }
}
