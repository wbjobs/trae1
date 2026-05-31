package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;

public class DdlConfig {

    @JsonProperty("policy")
    private DdlPolicy policy = DdlPolicy.AUTO;

    @JsonProperty("alert")
    private AlertConfig alert;

    @JsonProperty("autoApplySafeConversions")
    private boolean autoApplySafeConversions = true;

    @JsonProperty("pauseTableOnFailure")
    private boolean pauseTableOnFailure = true;

    @JsonProperty("stateStorePath")
    private String stateStorePath = "./data/ddl-state";

    public DdlPolicy getPolicy() {
        return policy;
    }

    public void setPolicy(DdlPolicy policy) {
        this.policy = policy;
    }

    public AlertConfig getAlert() {
        return alert;
    }

    public void setAlert(AlertConfig alert) {
        this.alert = alert;
    }

    public boolean isAutoApplySafeConversions() {
        return autoApplySafeConversions;
    }

    public void setAutoApplySafeConversions(boolean autoApplySafeConversions) {
        this.autoApplySafeConversions = autoApplySafeConversions;
    }

    public boolean isPauseTableOnFailure() {
        return pauseTableOnFailure;
    }

    public void setPauseTableOnFailure(boolean pauseTableOnFailure) {
        this.pauseTableOnFailure = pauseTableOnFailure;
    }

    public String getStateStorePath() {
        return stateStorePath;
    }

    public void setStateStorePath(String stateStorePath) {
        this.stateStorePath = stateStorePath;
    }

    public enum DdlPolicy {
        @JsonProperty("auto")
        AUTO,
        @JsonProperty("manual")
        MANUAL,
        @JsonProperty("skip")
        SKIP
    }

    public static class AlertConfig {

        @JsonProperty("enabled")
        private boolean enabled = false;

        @JsonProperty("dingtalk")
        private DingTalkConfig dingtalk;

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        public DingTalkConfig getDingtalk() {
            return dingtalk;
        }

        public void setDingtalk(DingTalkConfig dingtalk) {
            this.dingtalk = dingtalk;
        }
    }

    public static class DingTalkConfig {

        @JsonProperty("webhookUrl")
        private String webhookUrl;

        @JsonProperty("secret")
        private String secret;

        @JsonProperty("atMobiles")
        private String[] atMobiles = new String[0];

        @JsonProperty("atAll")
        private boolean atAll = false;

        public String getWebhookUrl() {
            return webhookUrl;
        }

        public void setWebhookUrl(String webhookUrl) {
            this.webhookUrl = webhookUrl;
        }

        public String getSecret() {
            return secret;
        }

        public void setSecret(String secret) {
            this.secret = secret;
        }

        public String[] getAtMobiles() {
            return atMobiles;
        }

        public void setAtMobiles(String[] atMobiles) {
            this.atMobiles = atMobiles;
        }

        public boolean isAtAll() {
            return atAll;
        }

        public void setAtAll(boolean atAll) {
            this.atAll = atAll;
        }
    }
}
