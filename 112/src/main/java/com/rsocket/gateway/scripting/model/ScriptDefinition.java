package com.rsocket.gateway.scripting.model;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.time.Instant;
import java.util.List;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class ScriptDefinition {

    public enum InterceptPoint {
        BEFORE,
        AFTER,
        ERROR
    }

    public enum FallbackStrategy {
        ALLOW,
        REJECT,
        LOG_ONLY
    }

    private String id;
    private String name;
    private String description;
    private String script;
    private String language;
    private InterceptPoint interceptPoint;
    private List<String> routes;
    private List<String> interactionTypes;
    private int priority;
    private boolean enabled;
    private int timeoutMs;
    private FallbackStrategy fallbackStrategy;
    private String version;
    private Instant createdAt;
    private Instant updatedAt;
    private String templateId;
}
