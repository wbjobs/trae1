package com.cdc.core.masking;

import com.cdc.common.config.DataMaskingConfig;
import com.cdc.common.config.DataMaskingConfig.MaskingRule;
import com.cdc.common.config.DataMaskingConfig.RowFilterRule;
import com.cdc.common.config.DataMaskingConfig.RuleRepositoryConfig;
import com.cdc.common.config.DataMaskingConfig.RepositoryType;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class RuleRepositoryManager {

    private static final Logger logger = LoggerFactory.getLogger(RuleRepositoryManager.class);

    private final DataMaskingConfig config;
    private final MaskingRuleEngine ruleEngine;
    private final ObjectMapper yamlMapper;
    private final ScheduledExecutorService reloadScheduler;
    private volatile String currentVersion = "unknown";
    private volatile boolean running = false;

    public RuleRepositoryManager(DataMaskingConfig config, MaskingRuleEngine ruleEngine) {
        this.config = config;
        this.ruleEngine = ruleEngine;
        this.yamlMapper = new ObjectMapper(new YAMLFactory());

        if (config.getRuleRepository() != null && config.getRuleRepository().isAutoReload()) {
            this.reloadScheduler = Executors.newSingleThreadScheduledExecutor(r -> {
                Thread t = new Thread(r, "rule-reload");
                t.setDaemon(true);
                return t;
            });
        } else {
            this.reloadScheduler = null;
        }
    }

    public void initialize() {
        if (config.getRuleRepository() == null) {
            logger.info("No rule repository configured, using inline rules");
            return;
        }

        RuleRepositoryConfig repoConfig = config.getRuleRepository();

        if (repoConfig.getType() == RepositoryType.GIT && repoConfig.getGit() != null) {
            cloneOrUpdateGitRepository();
        }

        loadRulesFromRepository();

        if (repoConfig.isAutoReload() && reloadScheduler != null) {
            startAutoReload(repoConfig.getReloadIntervalMs());
        }

        running = true;
        logger.info("Rule repository manager initialized, current version: {}", currentVersion);
    }

    private void cloneOrUpdateGitRepository() {
        var gitConfig = config.getRuleRepository().getGit();
        String localPath = config.getRuleRepository().getLocalPath();

        try {
            Path path = Paths.get(localPath);

            if (Files.exists(path) && Files.exists(path.resolve(".git"))) {
                logger.info("Pulling latest rules from git repository...");
                executeGitCommand(localPath, "pull", "origin", gitConfig.getBranch());
            } else {
                logger.info("Cloning rules from git repository: {}", gitConfig.getUrl());
                Files.createDirectories(path.getParent());
                executeGitCommand(
                        path.getParent().toString(),
                        "clone",
                        "-b", gitConfig.getBranch(),
                        gitConfig.getUrl(),
                        path.getFileName().toString()
                );
            }

            currentVersion = getCurrentGitVersion(localPath);
            logger.info("Git repository updated to version: {}", currentVersion);
        } catch (Exception e) {
            logger.error("Failed to update git repository", e);
        }
    }

    private void loadRulesFromRepository() {
        String localPath = config.getRuleRepository().getLocalPath();
        Path rulesPath = Paths.get(localPath);

        if (config.getRuleRepository().getType() == RepositoryType.GIT
                && config.getRuleRepository().getGit() != null) {
            String gitRulesPath = config.getRuleRepository().getGit().getRulesPath();
            if (gitRulesPath != null && !gitRulesPath.isEmpty()) {
                rulesPath = rulesPath.resolve(gitRulesPath.replaceFirst("^/", ""));
            }
        }

        if (!Files.exists(rulesPath)) {
            logger.warn("Rules directory not found: {}", rulesPath);
            return;
        }

        List<MaskingRule> maskingRules = new ArrayList<>();
        List<RowFilterRule> filterRules = new ArrayList<>();

        try {
            Files.list(rulesPath)
                    .filter(p -> p.toString().endsWith(".yaml") || p.toString().endsWith(".yml"))
                    .forEach(ruleFile -> {
                        try {
                            parseRuleFile(ruleFile.toFile(), maskingRules, filterRules);
                        } catch (Exception e) {
                            logger.error("Failed to parse rule file: {}", ruleFile, e);
                        }
                    });

            ruleEngine.reloadRules(maskingRules, filterRules);
            logger.info("Loaded {} masking rules and {} filter rules from repository",
                    maskingRules.size(), filterRules.size());
        } catch (Exception e) {
            logger.error("Failed to load rules from repository", e);
        }
    }

    private void parseRuleFile(File file, List<MaskingRule> maskingRules, List<RowFilterRule> filterRules) throws Exception {
        String content = Files.readString(file.toPath());

        if (content.contains("type:") && (content.contains("PHONE") || content.contains("ID_CARD") ||
                content.contains("EMAIL") || content.contains("CUSTOM"))) {
            MaskingRule rule = yamlMapper.readValue(content, MaskingRule.class);
            if (rule.getId() == null) {
                rule.setId(file.getName().replace(".yaml", "").replace(".yml", ""));
            }
            maskingRules.add(rule);
            logger.debug("Loaded masking rule: {} from {}", rule.getName(), file.getName());
        } else if (content.contains("condition:") || content.contains("action:")) {
            RowFilterRule rule = yamlMapper.readValue(content, RowFilterRule.class);
            if (rule.getId() == null) {
                rule.setId(file.getName().replace(".yaml", "").replace(".yml", ""));
            }
            filterRules.add(rule);
            logger.debug("Loaded filter rule: {} from {}", rule.getName(), file.getName());
        }
    }

    private void startAutoReload(long intervalMs) {
        reloadScheduler.scheduleAtFixedRate(() -> {
            try {
                logger.debug("Checking for rule updates...");

                if (config.getRuleRepository().getType() == RepositoryType.GIT) {
                    String localPath = config.getRuleRepository().getLocalPath();
                    String newVersion = getCurrentGitVersion(localPath);

                    if (!newVersion.equals(currentVersion)) {
                        logger.info("New rule version detected: {} -> {}", currentVersion, newVersion);
                        cloneOrUpdateGitRepository();
                        loadRulesFromRepository();
                    }
                } else {
                    loadRulesFromRepository();
                }
            } catch (Exception e) {
                logger.error("Failed to reload rules", e);
            }
        }, intervalMs, intervalMs, TimeUnit.MILLISECONDS);

        logger.info("Auto-reload enabled with interval: {}ms", intervalMs);
    }

    private String executeGitCommand(String workDir, String... command) throws Exception {
        ProcessBuilder pb = new ProcessBuilder("git", command);
        pb.directory(new File(workDir));
        pb.redirectErrorStream(true);

        Process process = pb.start();
        String output = new String(process.getInputStream().readAllBytes());
        int exitCode = process.waitFor();

        if (exitCode != 0) {
            throw new RuntimeException("Git command failed with exit code " + exitCode + ": " + output);
        }

        return output;
    }

    private String getCurrentGitVersion(String localPath) {
        try {
            return executeGitCommand(localPath, "rev-parse", "HEAD").trim();
        } catch (Exception e) {
            logger.warn("Failed to get git version: {}", e.getMessage());
            return "unknown";
        }
    }

    public void reloadNow() {
        logger.info("Manual rule reload triggered");
        if (config.getRuleRepository() != null) {
            if (config.getRuleRepository().getType() == RepositoryType.GIT) {
                cloneOrUpdateGitRepository();
            }
            loadRulesFromRepository();
        }
    }

    public String getCurrentVersion() {
        return currentVersion;
    }

    public boolean isRunning() {
        return running;
    }

    public void shutdown() {
        running = false;
        if (reloadScheduler != null) {
            reloadScheduler.shutdown();
            try {
                if (!reloadScheduler.awaitTermination(5, TimeUnit.SECONDS)) {
                    reloadScheduler.shutdownNow();
                }
            } catch (InterruptedException e) {
                reloadScheduler.shutdownNow();
                Thread.currentThread().interrupt();
            }
        }
        logger.info("Rule repository manager shut down");
    }

    public List<String> listAvailableRules() {
        List<String> rules = new ArrayList<>();
        if (config.getRuleRepository() == null) {
            return rules;
        }

        String localPath = config.getRuleRepository().getLocalPath();
        Path rulesPath = Paths.get(localPath);

        if (config.getRuleRepository().getType() == RepositoryType.GIT
                && config.getRuleRepository().getGit() != null) {
            String gitRulesPath = config.getRuleRepository().getGit().getRulesPath();
            if (gitRulesPath != null && !gitRulesPath.isEmpty()) {
                rulesPath = rulesPath.resolve(gitRulesPath.replaceFirst("^/", ""));
            }
        }

        if (!Files.exists(rulesPath)) {
            return rules;
        }

        try {
            Files.list(rulesPath)
                    .filter(p -> p.toString().endsWith(".yaml") || p.toString().endsWith(".yml"))
                    .forEach(p -> rules.add(p.getFileName().toString()));
        } catch (Exception e) {
            logger.error("Failed to list rules", e);
        }

        return rules;
    }
}
