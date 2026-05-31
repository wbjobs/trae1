package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;

public class MetricsConfig {

    @JsonProperty("enabled")
    private boolean enabled = true;

    @JsonProperty("port")
    private int port = 9090;

    @JsonProperty("path")
    private String path = "/metrics";

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public String getPath() {
        return path;
    }

    public void setPath(String path) {
        this.path = path;
    }
}
