package com.cdc.common.config;

import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.ArrayList;
import java.util.List;

public class DataMaskingConfig {

    @JsonProperty("enabled")
    private boolean enabled = false;

    @JsonProperty("rules")
    private List<MaskingRule> rules = new ArrayList<>();

    @JsonProperty("rowFilters")
    private List<RowFilterRule> rowFilters = new ArrayList<>();

    @JsonProperty("scriptTimeoutMs")
    private long scriptTimeoutMs = 50;

    @JsonProperty("degradeOnTimeout")
    private boolean degradeOnTimeout = true;

    @JsonProperty("ruleRepository")
    private RuleRepositoryConfig ruleRepository;

    @JsonProperty("cacheSize")
    private int cacheSize = 1000;

    public boolean isEnabled() {
        return enabled;
    }

    public void setEnabled(boolean enabled) {
        this.enabled = enabled;
    }

    public List<MaskingRule> getRules() {
        return rules;
    }

    public void setRules(List<MaskingRule> rules) {
        this.rules = rules;
    }

    public List<RowFilterRule> getRowFilters() {
        return rowFilters;
    }

    public void setRowFilters(List<RowFilterRule> rowFilters) {
        this.rowFilters = rowFilters;
    }

    public long getScriptTimeoutMs() {
        return scriptTimeoutMs;
    }

    public void setScriptTimeoutMs(long scriptTimeoutMs) {
        this.scriptTimeoutMs = scriptTimeoutMs;
    }

    public boolean isDegradeOnTimeout() {
        return degradeOnTimeout;
    }

    public void setDegradeOnTimeout(boolean degradeOnTimeout) {
        this.degradeOnTimeout = degradeOnTimeout;
    }

    public RuleRepositoryConfig getRuleRepository() {
        return ruleRepository;
    }

    public void setRuleRepository(RuleRepositoryConfig ruleRepository) {
        this.ruleRepository = ruleRepository;
    }

    public int getCacheSize() {
        return cacheSize;
    }

    public void setCacheSize(int cacheSize) {
        this.cacheSize = cacheSize;
    }

    public static class MaskingRule {

        @JsonProperty("id")
        private String id;

        @JsonProperty("name")
        private String name;

        @JsonProperty("tableName")
        private String tableName;

        @JsonProperty("columnName")
        private String columnName;

        @JsonProperty("type")
        private MaskingType type = MaskingType.CUSTOM;

        @JsonProperty("script")
        private String script;

        @JsonProperty("description")
        private String description;

        @JsonProperty("enabled")
        private boolean enabled = true;

        @JsonProperty("version")
        private int version = 1;

        public String getId() {
            return id;
        }

        public void setId(String id) {
            this.id = id;
        }

        public String getName() {
            return name;
        }

        public void setName(String name) {
            this.name = name;
        }

        public String getTableName() {
            return tableName;
        }

        public void setTableName(String tableName) {
            this.tableName = tableName;
        }

        public String getColumnName() {
            return columnName;
        }

        public void setColumnName(String columnName) {
            this.columnName = columnName;
        }

        public MaskingType getType() {
            return type;
        }

        public void setType(MaskingType type) {
            this.type = type;
        }

        public String getScript() {
            return script;
        }

        public void setScript(String script) {
            this.script = script;
        }

        public String getDescription() {
            return description;
        }

        public void setDescription(String description) {
            this.description = description;
        }

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        public int getVersion() {
            return version;
        }

        public void setVersion(int version) {
            this.version = version;
        }
    }

    public static class RowFilterRule {

        @JsonProperty("id")
        private String id;

        @JsonProperty("name")
        private String name;

        @JsonProperty("tableName")
        private String tableName;

        @JsonProperty("condition")
        private String condition;

        @JsonProperty("action")
        private FilterAction action = FilterAction.EXCLUDE;

        @JsonProperty("description")
        private String description;

        @JsonProperty("enabled")
        private boolean enabled = true;

        @JsonProperty("version")
        private int version = 1;

        public String getId() {
            return id;
        }

        public void setId(String id) {
            this.id = id;
        }

        public String getName() {
            return name;
        }

        public void setName(String name) {
            this.name = name;
        }

        public String getTableName() {
            return tableName;
        }

        public void setTableName(String tableName) {
            this.tableName = tableName;
        }

        public String getCondition() {
            return condition;
        }

        public void setCondition(String condition) {
            this.condition = condition;
        }

        public FilterAction getAction() {
            return action;
        }

        public void setAction(FilterAction action) {
            this.action = action;
        }

        public String getDescription() {
            return description;
        }

        public void setDescription(String description) {
            this.description = description;
        }

        public boolean isEnabled() {
            return enabled;
        }

        public void setEnabled(boolean enabled) {
            this.enabled = enabled;
        }

        public int getVersion() {
            return version;
        }

        public void setVersion(int version) {
            this.version = version;
        }
    }

    public static class RuleRepositoryConfig {

        @JsonProperty("type")
        private RepositoryType type = RepositoryType.LOCAL;

        @JsonProperty("git")
        private GitRepositoryConfig git;

        @JsonProperty("localPath")
        private String localPath = "./rules";

        @JsonProperty("autoReload")
        private boolean autoReload = true;

        @JsonProperty("reloadIntervalMs")
        private long reloadIntervalMs = 60000;

        public RepositoryType getType() {
            return type;
        }

        public void setType(RepositoryType type) {
            this.type = type;
        }

        public GitRepositoryConfig getGit() {
            return git;
        }

        public void setGit(GitRepositoryConfig git) {
            this.git = git;
        }

        public String getLocalPath() {
            return localPath;
        }

        public void setLocalPath(String localPath) {
            this.localPath = localPath;
        }

        public boolean isAutoReload() {
            return autoReload;
        }

        public void setAutoReload(boolean autoReload) {
            this.autoReload = autoReload;
        }

        public long getReloadIntervalMs() {
            return reloadIntervalMs;
        }

        public void setReloadIntervalMs(long reloadIntervalMs) {
            this.reloadIntervalMs = reloadIntervalMs;
        }
    }

    public static class GitRepositoryConfig {

        @JsonProperty("url")
        private String url;

        @JsonProperty("branch")
        private String branch = "main";

        @JsonProperty("username")
        private String username;

        @JsonProperty("password")
        private String password;

        @JsonProperty("sshKeyPath")
        private String sshKeyPath;

        @JsonProperty("rulesPath")
        private String rulesPath = "/rules";

        public String getUrl() {
            return url;
        }

        public void setUrl(String url) {
            this.url = url;
        }

        public String getBranch() {
            return branch;
        }

        public void setBranch(String branch) {
            this.branch = branch;
        }

        public String getUsername() {
            return username;
        }

        public void setUsername(String username) {
            this.username = username;
        }

        public String getPassword() {
            return password;
        }

        public void setPassword(String password) {
            this.password = password;
        }

        public String getSshKeyPath() {
            return sshKeyPath;
        }

        public void setSshKeyPath(String sshKeyPath) {
            this.sshKeyPath = sshKeyPath;
        }

        public String getRulesPath() {
            return rulesPath;
        }

        public void setRulesPath(String rulesPath) {
            this.rulesPath = rulesPath;
        }
    }

    public enum MaskingType {
        @JsonProperty("phone")
        PHONE,
        @JsonProperty("id_card")
        ID_CARD,
        @JsonProperty("email")
        EMAIL,
        @JsonProperty("name")
        NAME,
        @JsonProperty("bank_card")
        BANK_CARD,
        @JsonProperty("address")
        ADDRESS,
        @JsonProperty("full_mask")
        FULL_MASK,
        @JsonProperty("custom")
        CUSTOM
    }

    public enum FilterAction {
        @JsonProperty("include")
        INCLUDE,
        @JsonProperty("exclude")
        EXCLUDE
    }

    public enum RepositoryType {
        @JsonProperty("local")
        LOCAL,
        @JsonProperty("git")
        GIT
    }
}
