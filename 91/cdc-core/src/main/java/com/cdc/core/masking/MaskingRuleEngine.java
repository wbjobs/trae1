package com.cdc.core.masking;

import com.cdc.common.config.DataMaskingConfig;
import com.cdc.common.config.DataMaskingConfig.MaskingRule;
import com.cdc.common.config.DataMaskingConfig.MaskingType;
import com.cdc.common.config.DataMaskingConfig.RowFilterRule;
import com.cdc.common.event.CdcEvent;
import com.cdc.core.alert.DingTalkAlertService;
import groovy.lang.Binding;
import groovy.lang.GroovyShell;
import org.codehaus.groovy.control.CompilerConfiguration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.*;
import java.util.concurrent.*;
import java.util.regex.Pattern;

public class MaskingRuleEngine {

    private static final Logger logger = LoggerFactory.getLogger(MaskingRuleEngine.class);

    private final DataMaskingConfig config;
    private final DingTalkAlertService alertService;
    private final ExecutorService scriptExecutor;
    private final GroovyShell groovyShell;
    private final Map<String, List<MaskingRule>> maskingRuleCache = new ConcurrentHashMap<>();
    private final Map<String, List<RowFilterRule>> filterRuleCache = new ConcurrentHashMap<>();
    private final Map<String, Object> scriptCache = new ConcurrentHashMap<>();
    private final Map<String, Long> degradedRules = new ConcurrentHashMap<>();
    private volatile boolean initialized = false;

    public MaskingRuleEngine(DataMaskingConfig config, DingTalkAlertService alertService) {
        this.config = config;
        this.alertService = alertService;
        this.scriptExecutor = Executors.newFixedThreadPool(
                Runtime.getRuntime().availableProcessors(),
                r -> {
                    Thread t = new Thread(r, "masking-script");
                    t.setDaemon(true);
                    return t;
                }
        );

        CompilerConfiguration cc = new CompilerConfiguration();
        cc.setSourceEncoding("UTF-8");
        this.groovyShell = new GroovyShell(cc);

        initialize();
    }

    private void initialize() {
        if (!config.isEnabled()) {
            logger.info("Data masking is disabled");
            return;
        }

        logger.info("Initializing masking rule engine with {} rules and {} filters",
                config.getRules().size(), config.getRowFilters().size());

        for (MaskingRule rule : config.getRules()) {
            if (rule.isEnabled()) {
                maskingRuleCache.computeIfAbsent(rule.getTableName(), k -> new ArrayList<>()).add(rule);
                logger.debug("Loaded masking rule: {} for table {} column {}",
                        rule.getName(), rule.getTableName(), rule.getColumnName());
            }
        }

        for (RowFilterRule filter : config.getRowFilters()) {
            if (filter.isEnabled()) {
                filterRuleCache.computeIfAbsent(filter.getTableName(), k -> new ArrayList<>()).add(filter);
                logger.debug("Loaded filter rule: {} for table {}",
                        filter.getName(), filter.getTableName());
            }
        }

        initialized = true;
        logger.info("Masking rule engine initialized successfully");
    }

    public void reloadRules(List<MaskingRule> newRules, List<RowFilterRule> newFilters) {
        logger.info("Reloading masking rules...");

        maskingRuleCache.clear();
        filterRuleCache.clear();
        scriptCache.clear();

        if (newRules != null) {
            for (MaskingRule rule : newRules) {
                if (rule.isEnabled()) {
                    maskingRuleCache.computeIfAbsent(rule.getTableName(), k -> new ArrayList<>()).add(rule);
                }
            }
        }

        if (newFilters != null) {
            for (RowFilterRule filter : newFilters) {
                if (filter.isEnabled()) {
                    filterRuleCache.computeIfAbsent(filter.getTableName(), k -> new ArrayList<>()).add(filter);
                }
            }
        }

        logger.info("Rules reloaded: {} masking rules, {} filter rules",
                maskingRuleCache.values().stream().mapToLong(List::size).sum(),
                filterRuleCache.values().stream().mapToLong(List::size).sum());
    }

    public CdcEvent processEvent(CdcEvent event) {
        if (!config.isEnabled() || event == null) {
            return event;
        }

        String fullTableName = event.getFullTableName();

        if (shouldFilterRow(event, fullTableName)) {
            logger.debug("Row filtered out for table: {}", fullTableName);
            return null;
        }

        applyMasking(event, fullTableName);

        return event;
    }

    private boolean shouldFilterRow(CdcEvent event, String fullTableName) {
        List<RowFilterRule> filters = filterRuleCache.get(fullTableName);
        if (filters == null || filters.isEmpty()) {
            return false;
        }

        Map<String, Object> rowData = event.getAfter() != null ? event.getAfter() : event.getBefore();
        if (rowData == null) {
            return false;
        }

        for (RowFilterRule filter : filters) {
            try {
                boolean match = evaluateFilterCondition(filter.getCondition(), rowData);
                if (match) {
                    if (filter.getAction() == DataMaskingConfig.FilterAction.EXCLUDE) {
                        return true;
                    }
                } else {
                    if (filter.getAction() == DataMaskingConfig.FilterAction.INCLUDE) {
                        return true;
                    }
                }
            } catch (Exception e) {
                logger.error("Failed to evaluate filter condition for {}: {}", filter.getName(), e.getMessage());
            }
        }

        return false;
    }

    private boolean evaluateFilterCondition(String condition, Map<String, Object> rowData) {
        try {
            String script = buildFilterScript(condition, rowData);
            Future<Boolean> future = scriptExecutor.submit(() -> {
                Binding binding = new Binding();
                rowData.forEach(binding::setVariable);
                Object result = groovyShell.evaluate(script);
                return Boolean.TRUE.equals(result);
            });

            return future.get(config.getScriptTimeoutMs(), TimeUnit.MILLISECONDS);
        } catch (TimeoutException e) {
            handleScriptTimeout("filter: " + condition);
            return false;
        } catch (Exception e) {
            logger.error("Filter evaluation failed: {}", e.getMessage());
            return false;
        }
    }

    private String buildFilterScript(String condition, Map<String, Object> rowData) {
        StringBuilder sb = new StringBuilder();
        sb.append("import java.util.*;\n");
        sb.append("def data = [:]\n");
        rowData.forEach((k, v) -> {
            sb.append(String.format("data['%s'] = %s\n", k, formatValue(v)));
        });
        sb.append(condition.replaceAll("\\b([a-zA-Z_][a-zA-Z0-9_]*)\\b", "data['$1']"));
        return sb.toString();
    }

    private String formatValue(Object value) {
        if (value == null) {
            return "null";
        }
        if (value instanceof String) {
            return "'" + value.toString().replace("'", "\\'") + "'";
        }
        if (value instanceof Number) {
            return value.toString();
        }
        if (value instanceof Boolean) {
            return value.toString();
        }
        return "'" + value.toString() + "'";
    }

    private void applyMasking(CdcEvent event, String fullTableName) {
        List<MaskingRule> rules = maskingRuleCache.get(fullTableName);
        if (rules == null || rules.isEmpty()) {
            return;
        }

        if (event.getAfter() != null) {
            event.setAfter(maskRowData(event.getAfter(), rules, fullTableName));
        }
        if (event.getBefore() != null) {
            event.setBefore(maskRowData(event.getBefore(), rules, fullTableName));
        }
    }

    private Map<String, Object> maskRowData(Map<String, Object> rowData, List<MaskingRule> rules, String fullTableName) {
        Map<String, Object> maskedData = new HashMap<>(rowData);

        for (MaskingRule rule : rules) {
            String columnName = rule.getColumnName();
            if (!maskedData.containsKey(columnName)) {
                continue;
            }

            Object value = maskedData.get(columnName);
            if (value == null) {
                continue;
            }

            try {
                Object maskedValue = applyMaskingRule(rule, value.toString(), fullTableName);
                maskedData.put(columnName, maskedValue);
            } catch (Exception e) {
                logger.error("Failed to apply masking rule {} for column {}: {}",
                        rule.getName(), columnName, e.getMessage());
                if (config.isDegradeOnTimeout()) {
                    handleMaskingDegradation(rule, fullTableName);
                }
            }
        }

        return maskedData;
    }

    private Object applyMaskingRule(MaskingRule rule, String value, String fullTableName) throws Exception {
        String cacheKey = rule.getId() + "_" + rule.getVersion();

        if (degradedRules.containsKey(cacheKey)) {
            long lastDegrade = degradedRules.get(cacheKey);
            if (System.currentTimeMillis() - lastDegrade < 60000) {
                return value;
            } else {
                degradedRules.remove(cacheKey);
            }
        }

        Future<Object> future = scriptExecutor.submit(() -> {
            switch (rule.getType()) {
                case PHONE:
                    return MaskingFunctions.maskPhone(value);
                case ID_CARD:
                    return MaskingFunctions.maskIdCard(value);
                case EMAIL:
                    return MaskingFunctions.maskEmail(value);
                case NAME:
                    return MaskingFunctions.maskName(value);
                case BANK_CARD:
                    return MaskingFunctions.maskBankCard(value);
                case ADDRESS:
                    return MaskingFunctions.maskAddress(value);
                case FULL_MASK:
                    return MaskingFunctions.fullMask(value);
                case CUSTOM:
                default:
                    return executeCustomScript(rule.getScript(), value);
            }
        });

        try {
            return future.get(config.getScriptTimeoutMs(), TimeUnit.MILLISECONDS);
        } catch (TimeoutException e) {
            handleScriptTimeout("masking: " + rule.getName());
            if (config.isDegradeOnTimeout()) {
                degradedRules.put(cacheKey, System.currentTimeMillis());
                return value;
            }
            throw e;
        }
    }

    private Object executeCustomScript(String script, String value) {
        Binding binding = new Binding();
        binding.setVariable("value", value);
        binding.setVariable("MaskingFunctions", MaskingFunctions.class);
        return groovyShell.evaluate(script, binding);
    }

    private void handleScriptTimeout(String ruleName) {
        logger.warn("Script execution timed out for: {}", ruleName);
        if (alertService != null && alertService.isEnabled()) {
            try {
                alertService.sendAlert("CDC Masking Script Timeout",
                        String.format("Masking script execution timed out for rule: %s\nTimeout: %dms",
                                ruleName, config.getScriptTimeoutMs()));
            } catch (Exception e) {
                logger.error("Failed to send timeout alert", e);
            }
        }
    }

    private void handleMaskingDegradation(MaskingRule rule, String tableName) {
        logger.warn("Masking rule {} degraded for table {}", rule.getName(), tableName);
        if (alertService != null && alertService.isEnabled()) {
            try {
                alertService.sendAlert("CDC Masking Rule Degraded",
                        String.format("Masking rule %s degraded for table %s.%s\nOriginal value will be passed through.",
                                rule.getName(), tableName, rule.getColumnName()));
            } catch (Exception e) {
                logger.error("Failed to send degradation alert", e);
            }
        }
    }

    public Map<String, Object> testRule(MaskingRule rule, Object value) {
        Map<String, Object> result = new HashMap<>();
        long startTime = System.nanoTime();

        try {
            Object maskedValue = applyMaskingRule(rule, value.toString(), "test");
            long duration = System.nanoTime() - startTime;

            result.put("success", true);
            result.put("originalValue", value);
            result.put("maskedValue", maskedValue);
            result.put("durationMs", duration / 1_000_000.0);
            result.put("durationUs", duration / 1_000.0);
            result.put("timedOut", false);
            result.put("degraded", false);
        } catch (TimeoutException e) {
            long duration = System.nanoTime() - startTime;
            result.put("success", false);
            result.put("originalValue", value);
            result.put("maskedValue", value);
            result.put("durationMs", duration / 1_000_000.0);
            result.put("timedOut", true);
            result.put("error", "Execution timed out after " + config.getScriptTimeoutMs() + "ms");
        } catch (Exception e) {
            long duration = System.nanoTime() - startTime;
            result.put("success", false);
            result.put("originalValue", value);
            result.put("durationMs", duration / 1_000_000.0);
            result.put("error", e.getMessage());
        }

        return result;
    }

    public boolean testFilter(RowFilterRule filter, Map<String, Object> rowData) {
        return evaluateFilterCondition(filter.getCondition(), rowData);
    }

    public void shutdown() {
        scriptExecutor.shutdown();
        try {
            if (!scriptExecutor.awaitTermination(5, TimeUnit.SECONDS)) {
                scriptExecutor.shutdownNow();
            }
        } catch (InterruptedException e) {
            scriptExecutor.shutdownNow();
            Thread.currentThread().interrupt();
        }
        logger.info("Masking rule engine shut down");
    }

    public Map<String, Object> getStatus() {
        Map<String, Object> status = new HashMap<>();
        status.put("enabled", config.isEnabled());
        status.put("initialized", initialized);
        status.put("totalMaskingRules", maskingRuleCache.values().stream().mapToLong(List::size).sum());
        status.put("totalFilterRules", filterRuleCache.values().stream().mapToLong(List::size).sum());
        status.put("scriptTimeoutMs", config.getScriptTimeoutMs());
        status.put("degradedRules", degradedRules.size());
        status.put("tablesWithRules", maskingRuleCache.keySet());
        status.put("tablesWithFilters", filterRuleCache.keySet());
        return status;
    }
}
