package com.dbagent.config;

public class AgentConfig {

    private static final String DEFAULT_OTEL_EXPORTER_ENDPOINT = "http://localhost:4317";
    private static final String DEFAULT_SERVICE_NAME = "db-tracing-agent";
    private static final double DEFAULT_SAMPLE_RATE = 0.01;
    private static final String DEFAULT_OTEL_PROPAGATORS = "tracecontext,baggage";
    private static final String DEFAULT_MASKING_CONFIG_PATH = "masking-rules.json";
    private static final long DEFAULT_MASKING_RELOAD_INTERVAL_MS = 30000;
    private static final long DEFAULT_SLOW_SQL_THRESHOLD_MS = 200;
    private static final long DEFAULT_INDEX_SUGGESTION_COOLDOWN_MS = 300000;
    private static final String DEFAULT_WEBHOOK_URL = "";
    private static final String DEFAULT_WEBHOOK_TYPE = "generic";

    private final String otelExporterEndpoint;
    private final String serviceName;
    private final double sampleRate;
    private final String otelPropagators;
    private final boolean logSql;
    private final boolean captureStack;
    private final String maskingConfigPath;
    private final long maskingReloadIntervalMs;
    private final boolean maskingHotReload;
    private final boolean maskingEnabled;
    private final long slowSqlThresholdMs;
    private final long indexSuggestionCooldownMs;
    private final String webhookUrl;
    private final String webhookType;
    private final String webhookSecret;
    private final boolean indexAutoExecute;
    private final boolean indexAdvisorEnabled;

    private AgentConfig(Builder builder) {
        this.otelExporterEndpoint = builder.otelExporterEndpoint;
        this.serviceName = builder.serviceName;
        this.sampleRate = builder.sampleRate;
        this.otelPropagators = builder.otelPropagators;
        this.logSql = builder.logSql;
        this.captureStack = builder.captureStack;
        this.maskingConfigPath = builder.maskingConfigPath;
        this.maskingReloadIntervalMs = builder.maskingReloadIntervalMs;
        this.maskingHotReload = builder.maskingHotReload;
        this.maskingEnabled = builder.maskingEnabled;
        this.slowSqlThresholdMs = builder.slowSqlThresholdMs;
        this.indexSuggestionCooldownMs = builder.indexSuggestionCooldownMs;
        this.webhookUrl = builder.webhookUrl;
        this.webhookType = builder.webhookType;
        this.webhookSecret = builder.webhookSecret;
        this.indexAutoExecute = builder.indexAutoExecute;
        this.indexAdvisorEnabled = builder.indexAdvisorEnabled;
    }

    public static AgentConfig fromArgs(String agentArgs) {
        Builder builder = new Builder();
        if (agentArgs != null && !agentArgs.isEmpty()) {
            String[] args = agentArgs.split(",");
            for (String arg : args) {
                String[] kv = arg.split("=", 2);
                if (kv.length == 2) {
                    String key = kv[0].trim();
                    String value = kv[1].trim();
                    switch (key) {
                        case "otel.exporter.endpoint":
                            builder.otelExporterEndpoint(value);
                            break;
                        case "service.name":
                            builder.serviceName(value);
                            break;
                        case "sample.rate":
                            try {
                                builder.sampleRate(Double.parseDouble(value));
                            } catch (NumberFormatException e) {
                                System.err.println("[DB-Tracing-Agent] Invalid sample rate: " + value + ", using default: " + DEFAULT_SAMPLE_RATE);
                            }
                            break;
                        case "otel.propagators":
                            builder.otelPropagators(value);
                            break;
                        case "log.sql":
                            builder.logSql(Boolean.parseBoolean(value));
                            break;
                        case "capture.stack":
                            builder.captureStack(Boolean.parseBoolean(value));
                            break;
                        case "masking.config.path":
                            builder.maskingConfigPath(value);
                            break;
                        case "masking.hot.reload":
                            builder.maskingHotReload(Boolean.parseBoolean(value));
                            break;
                        case "masking.reload.interval.ms":
                            try {
                                builder.maskingReloadIntervalMs(Long.parseLong(value));
                            } catch (NumberFormatException e) {
                                System.err.println("[DB-Tracing-Agent] Invalid masking reload interval: " + value + ", using default: " + DEFAULT_MASKING_RELOAD_INTERVAL_MS);
                            }
                            break;
                        case "masking.enabled":
                            builder.maskingEnabled(Boolean.parseBoolean(value));
                            break;
                        case "slow.sql.threshold.ms":
                            try {
                                builder.slowSqlThresholdMs(Long.parseLong(value));
                            } catch (NumberFormatException e) {
                                System.err.println("[DB-Tracing-Agent] Invalid slow SQL threshold: " + value + ", using default: " + DEFAULT_SLOW_SQL_THRESHOLD_MS);
                            }
                            break;
                        case "index.suggestion.cooldown.ms":
                            try {
                                builder.indexSuggestionCooldownMs(Long.parseLong(value));
                            } catch (NumberFormatException e) {
                                System.err.println("[DB-Tracing-Agent] Invalid index suggestion cooldown: " + value + ", using default: " + DEFAULT_INDEX_SUGGESTION_COOLDOWN_MS);
                            }
                            break;
                        case "webhook.url":
                            builder.webhookUrl(value);
                            break;
                        case "webhook.type":
                            builder.webhookType(value);
                            break;
                        case "webhook.secret":
                            builder.webhookSecret(value);
                            break;
                        case "index.auto.execute":
                            builder.indexAutoExecute(Boolean.parseBoolean(value));
                            break;
                        case "index.advisor.enabled":
                            builder.indexAdvisorEnabled(Boolean.parseBoolean(value));
                            break;
                        default:
                            System.err.println("[DB-Tracing-Agent] Unknown parameter: " + key);
                    }
                }
            }
        }
        return builder.build();
    }

    public String getOtelExporterEndpoint() {
        return otelExporterEndpoint;
    }

    public String getServiceName() {
        return serviceName;
    }

    public double getSampleRate() {
        return sampleRate;
    }

    public String getOtelPropagators() {
        return otelPropagators;
    }

    public boolean isLogSql() {
        return logSql;
    }

    public boolean isCaptureStack() {
        return captureStack;
    }

    public String getMaskingConfigPath() {
        return maskingConfigPath;
    }

    public long getMaskingReloadIntervalMs() {
        return maskingReloadIntervalMs;
    }

    public boolean isMaskingHotReload() {
        return maskingHotReload;
    }

    public boolean isMaskingEnabled() {
        return maskingEnabled;
    }

    public long getSlowSqlThresholdMs() {
        return slowSqlThresholdMs;
    }

    public long getIndexSuggestionCooldownMs() {
        return indexSuggestionCooldownMs;
    }

    public String getWebhookUrl() {
        return webhookUrl;
    }

    public String getWebhookType() {
        return webhookType;
    }

    public String getWebhookSecret() {
        return webhookSecret;
    }

    public boolean isIndexAutoExecute() {
        return indexAutoExecute;
    }

    public boolean isIndexAdvisorEnabled() {
        return indexAdvisorEnabled;
    }

    public static class Builder {
        private String otelExporterEndpoint = DEFAULT_OTEL_EXPORTER_ENDPOINT;
        private String serviceName = DEFAULT_SERVICE_NAME;
        private double sampleRate = DEFAULT_SAMPLE_RATE;
        private String otelPropagators = DEFAULT_OTEL_PROPAGATORS;
        private boolean logSql = true;
        private boolean captureStack = true;
        private String maskingConfigPath = DEFAULT_MASKING_CONFIG_PATH;
        private long maskingReloadIntervalMs = DEFAULT_MASKING_RELOAD_INTERVAL_MS;
        private boolean maskingHotReload = true;
        private boolean maskingEnabled = true;
        private long slowSqlThresholdMs = DEFAULT_SLOW_SQL_THRESHOLD_MS;
        private long indexSuggestionCooldownMs = DEFAULT_INDEX_SUGGESTION_COOLDOWN_MS;
        private String webhookUrl = DEFAULT_WEBHOOK_URL;
        private String webhookType = DEFAULT_WEBHOOK_TYPE;
        private String webhookSecret = null;
        private boolean indexAutoExecute = false;
        private boolean indexAdvisorEnabled = true;

        public Builder otelExporterEndpoint(String otelExporterEndpoint) {
            this.otelExporterEndpoint = otelExporterEndpoint;
            return this;
        }

        public Builder serviceName(String serviceName) {
            this.serviceName = serviceName;
            return this;
        }

        public Builder sampleRate(double sampleRate) {
            if (sampleRate < 0.0 || sampleRate > 1.0) {
                throw new IllegalArgumentException("Sample rate must be between 0.0 and 1.0");
            }
            this.sampleRate = sampleRate;
            return this;
        }

        public Builder otelPropagators(String otelPropagators) {
            this.otelPropagators = otelPropagators;
            return this;
        }

        public Builder logSql(boolean logSql) {
            this.logSql = logSql;
            return this;
        }

        public Builder captureStack(boolean captureStack) {
            this.captureStack = captureStack;
            return this;
        }

        public Builder maskingConfigPath(String maskingConfigPath) {
            this.maskingConfigPath = maskingConfigPath;
            return this;
        }

        public Builder maskingReloadIntervalMs(long maskingReloadIntervalMs) {
            if (maskingReloadIntervalMs < 1000) {
                maskingReloadIntervalMs = 1000;
            }
            this.maskingReloadIntervalMs = maskingReloadIntervalMs;
            return this;
        }

        public Builder maskingHotReload(boolean maskingHotReload) {
            this.maskingHotReload = maskingHotReload;
            return this;
        }

        public Builder maskingEnabled(boolean maskingEnabled) {
            this.maskingEnabled = maskingEnabled;
            return this;
        }

        public Builder slowSqlThresholdMs(long slowSqlThresholdMs) {
            if (slowSqlThresholdMs < 10) {
                slowSqlThresholdMs = 10;
            }
            this.slowSqlThresholdMs = slowSqlThresholdMs;
            return this;
        }

        public Builder indexSuggestionCooldownMs(long indexSuggestionCooldownMs) {
            if (indexSuggestionCooldownMs < 1000) {
                indexSuggestionCooldownMs = 1000;
            }
            this.indexSuggestionCooldownMs = indexSuggestionCooldownMs;
            return this;
        }

        public Builder webhookUrl(String webhookUrl) {
            this.webhookUrl = webhookUrl;
            return this;
        }

        public Builder webhookType(String webhookType) {
            this.webhookType = webhookType;
            return this;
        }

        public Builder webhookSecret(String webhookSecret) {
            this.webhookSecret = webhookSecret;
            return this;
        }

        public Builder indexAutoExecute(boolean indexAutoExecute) {
            this.indexAutoExecute = indexAutoExecute;
            return this;
        }

        public Builder indexAdvisorEnabled(boolean indexAdvisorEnabled) {
            this.indexAdvisorEnabled = indexAdvisorEnabled;
            return this;
        }

        public AgentConfig build() {
            return new AgentConfig(this);
        }
    }

    @Override
    public String toString() {
        return "AgentConfig{" +
                "otelExporterEndpoint='" + otelExporterEndpoint + '\'' +
                ", serviceName='" + serviceName + '\'' +
                ", sampleRate=" + sampleRate +
                ", otelPropagators='" + otelPropagators + '\'' +
                ", logSql=" + logSql +
                ", captureStack=" + captureStack +
                ", maskingConfigPath='" + maskingConfigPath + '\'' +
                ", maskingReloadIntervalMs=" + maskingReloadIntervalMs +
                ", maskingHotReload=" + maskingHotReload +
                ", maskingEnabled=" + maskingEnabled +
                ", slowSqlThresholdMs=" + slowSqlThresholdMs +
                ", indexSuggestionCooldownMs=" + indexSuggestionCooldownMs +
                ", webhookUrl='" + webhookUrl + '\'' +
                ", webhookType='" + webhookType + '\'' +
                ", indexAutoExecute=" + indexAutoExecute +
                ", indexAdvisorEnabled=" + indexAdvisorEnabled +
                '}';
    }
}
