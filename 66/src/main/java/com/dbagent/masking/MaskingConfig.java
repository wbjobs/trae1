package com.dbagent.masking;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public class MaskingConfig {

    private static final String DEFAULT_CONFIG_PATH = "masking-rules.json";
    private static final long DEFAULT_RELOAD_INTERVAL_MS = 30000;

    private static volatile MaskingConfig instance;
    private static final Object LOCK = new Object();

    private final String configPath;
    private final long reloadIntervalMs;
    private final AtomicReference<SensitiveDataMasker> currentMasker;
    private final AtomicReference<Long> lastModified;
    private ScheduledExecutorService watcherExecutor;
    private volatile boolean hotReloadEnabled;

    private MaskingConfig(String configPath, long reloadIntervalMs) {
        this.configPath = configPath;
        this.reloadIntervalMs = reloadIntervalMs;
        this.currentMasker = new AtomicReference<>(loadMasker());
        this.lastModified = new AtomicReference<>(0L);
    }

    public static MaskingConfig getInstance() {
        return getInstance(DEFAULT_CONFIG_PATH, DEFAULT_RELOAD_INTERVAL_MS);
    }

    public static MaskingConfig getInstance(String configPath) {
        return getInstance(configPath, DEFAULT_RELOAD_INTERVAL_MS);
    }

    public static MaskingConfig getInstance(String configPath, long reloadIntervalMs) {
        if (instance == null) {
            synchronized (LOCK) {
                if (instance == null) {
                    instance = new MaskingConfig(
                            configPath != null ? configPath : DEFAULT_CONFIG_PATH,
                            reloadIntervalMs > 0 ? reloadIntervalMs : DEFAULT_RELOAD_INTERVAL_MS);
                }
            }
        }
        return instance;
    }

    public SensitiveDataMasker getMasker() {
        return currentMasker.get();
    }

    public synchronized void reload() {
        SensitiveDataMasker newMasker = loadMasker();
        if (newMasker != null) {
            currentMasker.set(newMasker);
            System.out.println("[DB-Tracing-Agent] Masking rules reloaded from: " + configPath);
        }
    }

    public void enableHotReload() {
        if (hotReloadEnabled) {
            return;
        }
        hotReloadEnabled = true;
        watcherExecutor = Executors.newSingleThreadScheduledExecutor(r -> {
            Thread t = new Thread(r, "masking-config-watcher");
            t.setDaemon(true);
            return t;
        });
        watcherExecutor.scheduleAtFixedRate(this::checkAndReload,
                reloadIntervalMs, reloadIntervalMs, TimeUnit.MILLISECONDS);
        System.out.println("[DB-Tracing-Agent] Masking config hot-reload enabled, interval: " + reloadIntervalMs + "ms");
    }

    public void disableHotReload() {
        hotReloadEnabled = false;
        if (watcherExecutor != null) {
            watcherExecutor.shutdown();
            watcherExecutor = null;
        }
        System.out.println("[DB-Tracing-Agent] Masking config hot-reload disabled");
    }

    public boolean isHotReloadEnabled() {
        return hotReloadEnabled;
    }

    public String getConfigPath() {
        return configPath;
    }

    private void checkAndReload() {
        try {
            Path path = Paths.get(configPath);
            if (!Files.exists(path)) {
                return;
            }
            long currentModified = Files.getLastModifiedTime(path).toMillis();
            if (currentModified != lastModified.get()) {
                lastModified.set(currentModified);
                reload();
            }
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Error checking masking config: " + e.getMessage());
        }
    }

    private SensitiveDataMasker loadMasker() {
        try {
            Path path = Paths.get(configPath);
            if (!Files.exists(path)) {
                System.out.println("[DB-Tracing-Agent] Masking config file not found: " + configPath + ", using built-in defaults");
                return createDefaultMasker();
            }

            lastModified.set(Files.getLastModifiedTime(path).toMillis());

            String json = new String(Files.readAllBytes(path), StandardCharsets.UTF_8);
            List<SensitiveDataMasker.MaskingRule> rules = parseRules(json);

            SensitiveDataMasker masker = new SensitiveDataMasker();
            for (SensitiveDataMasker.MaskingRule rule : rules) {
                if (rule.isEnabled()) {
                    masker.addRule(rule);
                }
            }

            System.out.println("[DB-Tracing-Agent] Loaded " + masker.getRules().size() + " masking rules from: " + configPath);
            return masker;
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Error loading masking config: " + e.getMessage());
            return createDefaultMasker();
        }
    }

    static List<SensitiveDataMasker.MaskingRule> parseRules(String json) {
        List<SensitiveDataMasker.MaskingRule> rules = new ArrayList<>();

        json = json.trim();
        if (!json.startsWith("[") && !json.startsWith("{")) {
            return rules;
        }

        String rulesArray;
        if (json.startsWith("{")) {
            int rulesIdx = json.indexOf("\"rules\"");
            if (rulesIdx < 0) {
                rulesIdx = json.indexOf("\"Rules\"");
            }
            if (rulesIdx < 0) {
                return rules;
            }
            int arrayStart = json.indexOf('[', rulesIdx);
            int arrayEnd = findMatchingBracket(json, arrayStart);
            rulesArray = json.substring(arrayStart, arrayEnd + 1);
        } else {
            rulesArray = json;
        }

        List<String> ruleObjects = extractJsonArrayElements(rulesArray);
        for (String ruleObj : ruleObjects) {
            SensitiveDataMasker.MaskingRule rule = parseSingleRule(ruleObj);
            if (rule != null) {
                rules.add(rule);
            }
        }

        return rules;
    }

    private static SensitiveDataMasker.MaskingRule parseSingleRule(String ruleObj) {
        SensitiveDataMasker.MaskingRule rule = new SensitiveDataMasker.MaskingRule();

        rule.setName(extractJsonString(ruleObj, "name"));
        rule.setDescription(extractJsonString(ruleObj, "description"));
        rule.setSqlPattern(extractJsonString(ruleObj, "sqlPattern"));
        rule.setFieldPattern(extractJsonString(ruleObj, "fieldPattern"));
        rule.setValuePattern(extractJsonString(ruleObj, "valuePattern"));
        rule.setTargetField(extractJsonString(ruleObj, "targetField"));

        String groupStr = extractJsonString(ruleObj, "replaceGroup");
        if (groupStr == null) {
            groupStr = extractJsonNumber(ruleObj, "replaceGroup");
        }
        if (groupStr != null) {
            try {
                rule.setReplaceGroup(Integer.parseInt(groupStr));
            } catch (NumberFormatException e) {
                rule.setReplaceGroup(1);
            }
        }

        String enabledStr = extractJsonString(ruleObj, "enabled");
        if (enabledStr == null) {
            enabledStr = extractJsonBoolean(ruleObj, "enabled");
        }
        if (enabledStr != null) {
            rule.setEnabled(Boolean.parseBoolean(enabledStr));
        }

        return rule;
    }

    private static String extractJsonString(String json, String key) {
        String pattern = "\"" + key + "\"\\s*:\\s*\"([^\"]*)\"";
        java.util.regex.Pattern p = java.util.regex.Pattern.compile(pattern);
        java.util.regex.Matcher m = p.matcher(json);
        if (m.find()) {
            return m.group(1);
        }
        return null;
    }

    private static String extractJsonNumber(String json, String key) {
        String pattern = "\"" + key + "\"\\s*:\\s*(\\d+)";
        java.util.regex.Pattern p = java.util.regex.Pattern.compile(pattern);
        java.util.regex.Matcher m = p.matcher(json);
        if (m.find()) {
            return m.group(1);
        }
        return null;
    }

    private static String extractJsonBoolean(String json, String key) {
        String pattern = "\"" + key + "\"\\s*:\\s*(true|false)";
        java.util.regex.Pattern p = java.util.regex.Pattern.compile(pattern);
        java.util.regex.Matcher m = p.matcher(json);
        if (m.find()) {
            return m.group(1);
        }
        return null;
    }

    private static int findMatchingBracket(String str, int start) {
        int count = 1;
        for (int i = start + 1; i < str.length(); i++) {
            char c = str.charAt(i);
            if (c == '[') count++;
            else if (c == ']') {
                count--;
                if (count == 0) return i;
            }
        }
        return str.length() - 1;
    }

    private static int findMatchingBrace(String str, int start) {
        int count = 1;
        for (int i = start + 1; i < str.length(); i++) {
            char c = str.charAt(i);
            if (c == '{') count++;
            else if (c == '}') {
                count--;
                if (count == 0) return i;
            }
        }
        return str.length() - 1;
    }

    private static List<String> extractJsonArrayElements(String arrayStr) {
        List<String> elements = new ArrayList<>();
        int start = arrayStr.indexOf('[');
        int end = arrayStr.lastIndexOf(']');
        if (start < 0 || end < 0 || start >= end) {
            return elements;
        }

        int i = start + 1;
        while (i < end) {
            while (i < end && Character.isWhitespace(arrayStr.charAt(i))) {
                i++;
            }
            if (i >= end) break;

            if (arrayStr.charAt(i) == '{') {
                int objEnd = findMatchingBrace(arrayStr, i);
                elements.add(arrayStr.substring(i, objEnd + 1));
                i = objEnd + 1;
            } else if (arrayStr.charAt(i) == ',') {
                i++;
            } else {
                i++;
            }
        }
        return elements;
    }

    private SensitiveDataMasker createDefaultMasker() {
        SensitiveDataMasker masker = new SensitiveDataMasker();

        SensitiveDataMasker.MaskingRule passwordRule = new SensitiveDataMasker.MaskingRule();
        passwordRule.setName("password-masking");
        passwordRule.setDescription("Mask password fields in SQL");
        passwordRule.setFieldPattern("password|passwd|pwd|pass");
        passwordRule.setEnabled(true);
        masker.addRule(passwordRule);

        SensitiveDataMasker.MaskingRule phoneRule = new SensitiveDataMasker.MaskingRule();
        phoneRule.setName("phone-masking");
        phoneRule.setDescription("Mask phone/mobile fields");
        phoneRule.setFieldPattern("phone|mobile|cell|tel|telephone");
        phoneRule.setEnabled(true);
        masker.addRule(phoneRule);

        SensitiveDataMasker.MaskingRule idCardRule = new SensitiveDataMasker.MaskingRule();
        idCardRule.setName("id-card-masking");
        idCardRule.setDescription("Mask ID card numbers");
        idCardRule.setFieldPattern("id_card|idcard|identity_card|身份证|证件号|证件号码");
        idCardRule.setEnabled(true);
        masker.addRule(idCardRule);

        SensitiveDataMasker.MaskingRule emailRule = new SensitiveDataMasker.MaskingRule();
        emailRule.setName("email-masking");
        emailRule.setDescription("Mask email fields");
        emailRule.setFieldPattern("email|e_mail|mail");
        emailRule.setEnabled(true);
        masker.addRule(emailRule);

        SensitiveDataMasker.MaskingRule bankCardRule = new SensitiveDataMasker.MaskingRule();
        bankCardRule.setName("bank-card-masking");
        bankCardRule.setDescription("Mask bank card numbers");
        bankCardRule.setFieldPattern("bank_card|bankcard|card_no|card_number|银行卡|卡号");
        bankCardRule.setEnabled(true);
        masker.addRule(bankCardRule);

        SensitiveDataMasker.MaskingRule tokenRule = new SensitiveDataMasker.MaskingRule();
        tokenRule.setName("token-masking");
        tokenRule.setDescription("Mask token/secret fields");
        tokenRule.setFieldPattern("token|secret|api_key|apikey|access_key|secret_key");
        tokenRule.setEnabled(true);
        masker.addRule(tokenRule);

        SensitiveDataMasker.MaskingRule ssnRule = new SensitiveDataMasker.MaskingRule();
        ssnRule.setName("ssn-masking");
        ssnRule.setDescription("Mask SSN/social security numbers");
        ssnRule.setFieldPattern("ssn|social_security|social_no");
        ssnRule.setEnabled(true);
        masker.addRule(ssnRule);

        return masker;
    }

    public void shutdown() {
        disableHotReload();
    }
}
