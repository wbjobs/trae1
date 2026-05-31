package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;
import java.util.HashMap;
import java.util.Map;

public class SchemaRegistryConfig {

    @JsonProperty("url")
    private String url = "http://localhost:8081";

    @JsonProperty("compatibilityLevel")
    private CompatibilityLevel compatibilityLevel = CompatibilityLevel.BACKWARD;

    @JsonProperty("autoRegister")
    private boolean autoRegister = true;

    @JsonProperty("properties")
    private Map<String, String> properties = new HashMap<>();

    public String getUrl() {
        return url;
    }

    public void setUrl(String url) {
        this.url = url;
    }

    public CompatibilityLevel getCompatibilityLevel() {
        return compatibilityLevel;
    }

    public void setCompatibilityLevel(CompatibilityLevel compatibilityLevel) {
        this.compatibilityLevel = compatibilityLevel;
    }

    public boolean isAutoRegister() {
        return autoRegister;
    }

    public void setAutoRegister(boolean autoRegister) {
        this.autoRegister = autoRegister;
    }

    public Map<String, String> getProperties() {
        return properties;
    }

    public void setProperties(Map<String, String> properties) {
        this.properties = properties;
    }

    public enum CompatibilityLevel {
        @JsonProperty("backward")
        BACKWARD,
        @JsonProperty("backward_transitive")
        BACKWARD_TRANSITIVE,
        @JsonProperty("forward")
        FORWARD,
        @JsonProperty("forward_transitive")
        FORWARD_TRANSITIVE,
        @JsonProperty("full")
        FULL,
        @JsonProperty("full_transitive")
        FULL_TRANSITIVE,
        @JsonProperty("none")
        NONE
    }
}
