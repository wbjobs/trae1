package com.cdc.cli.command;

import com.cdc.common.config.DataMaskingConfig;
import com.cdc.common.config.DataMaskingConfig.MaskingRule;
import com.cdc.common.config.DataMaskingConfig.MaskingType;
import com.cdc.core.masking.MaskingRuleEngine;
import com.cdc.core.masking.MaskingFunctions;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import picocli.CommandLine;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.Callable;

@CommandLine.Command(
        name = "masking-test",
        description = "Test data masking rules",
        mixinStandardHelpOptions = true
)
public class MaskingTestCommand implements Callable<Integer> {

    private static final Logger logger = LoggerFactory.getLogger(MaskingTestCommand.class);

    @CommandLine.Option(names = {"--type"}, description = "Masking type: phone, id_card, email, name, bank_card, address, full_mask", required = true)
    private MaskingType type;

    @CommandLine.Option(names = {"--value"}, description = "Value to mask", required = true)
    private String value;

    @CommandLine.Option(names = {"--script"}, description = "Custom Groovy script for masking")
    private String customScript;

    @CommandLine.Option(names = {"--benchmark"}, description = "Run benchmark with N iterations")
    private int benchmarkIterations;

    @Override
    public Integer call() {
        try {
            DataMaskingConfig config = new DataMaskingConfig();
            config.setEnabled(true);
            config.setScriptTimeoutMs(50);

            MaskingRuleEngine engine = new MaskingRuleEngine(config, null);

            MaskingRule rule = new MaskingRule();
            rule.setId("test-rule");
            rule.setName("Test Rule");
            rule.setType(type);
            rule.setTableName("test.table");
            rule.setColumnName("test_column");

            if (type == MaskingType.CUSTOM && customScript != null) {
                rule.setScript(customScript);
            }

            System.out.println("=".repeat(60));
            System.out.println("Masking Test");
            System.out.println("=".repeat(60));
            System.out.println("Type: " + type);
            System.out.println("Original Value: " + value);
            System.out.println("-".repeat(60));

            Map<String, Object> result = engine.testRule(rule, value);

            System.out.println("Result:");
            System.out.println("  Success: " + result.get("success"));
            System.out.println("  Original: " + result.get("originalValue"));
            System.out.println("  Masked: " + result.get("maskedValue"));
            System.out.println("  Duration (ms): " + result.get("durationMs"));
            System.out.println("  Duration (us): " + result.get("durationUs"));

            if (result.containsKey("timedOut")) {
                System.out.println("  Timed Out: " + result.get("timedOut"));
            }
            if (result.containsKey("error")) {
                System.out.println("  Error: " + result.get("error"));
            }

            System.out.println("-".repeat(60));

            if (benchmarkIterations > 0) {
                runBenchmark(rule, engine);
            }

            engine.shutdown();

            return (Boolean) result.get("success") ? 0 : 1;

        } catch (Exception e) {
            logger.error("Masking test failed", e);
            System.err.println("Error: " + e.getMessage());
            return 2;
        }
    }

    private void runBenchmark(MaskingRule rule, MaskingRuleEngine engine) {
        System.out.println("\nBenchmark (" + benchmarkIterations + " iterations):");
        System.out.println("-".repeat(60));

        long totalTime = 0;
        long minTime = Long.MAX_VALUE;
        long maxTime = Long.MIN_VALUE;
        int successCount = 0;
        int timeoutCount = 0;

        for (int i = 0; i < benchmarkIterations; i++) {
            long startTime = System.nanoTime();

            Map<String, Object> result = engine.testRule(rule, value);

            long duration = System.nanoTime() - startTime;
            totalTime += duration;
            minTime = Math.min(minTime, duration);
            maxTime = Math.max(maxTime, duration);

            if (Boolean.TRUE.equals(result.get("success"))) {
                successCount++;
            }
            if (Boolean.TRUE.equals(result.get("timedOut"))) {
                timeoutCount++;
            }
        }

        double avgTimeUs = (totalTime / benchmarkIterations) / 1_000.0;
        double avgTimeMs = avgTimeUs / 1_000.0;

        System.out.printf("  Average: %.2f us (%.4f ms)%n", avgTimeUs, avgTimeMs);
        System.out.printf("  Min: %.2f us%n", minTime / 1_000.0);
        System.out.printf("  Max: %.2f us%n", maxTime / 1_000.0);
        System.out.printf("  Success: %d/%d%n", successCount, benchmarkIterations);
        System.out.printf("  Timeouts: %d/%d%n", timeoutCount, benchmarkIterations);

        if (avgTimeMs < 1.0) {
            System.out.println("\n  Performance: PASSED (< 1ms per operation)");
        } else {
            System.out.println("\n  Performance: FAILED (>= 1ms per operation)");
        }
        System.out.println("=".repeat(60));
    }

    @CommandLine.Command(name = "list-functions", description = "List available masking functions")
    public Integer listFunctions() {
        System.out.println("Available Masking Functions:");
        System.out.println("=".repeat(60));
        System.out.println("1. phone - Mask phone number (138****1234)");
        System.out.println("2. id_card - Mask ID card number (110101********1234)");
        System.out.println("3. email - Mask email address (te***@example.com)");
        System.out.println("4. name - Mask personal name (张**)");
        System.out.println("5. bank_card - Mask bank card number (6222********1234)");
        System.out.println("6. address - Mask address (北京市***海淀区)");
        System.out.println("7. full_mask - Replace entire value with ***");
        System.out.println("8. custom - Use custom Groovy script");
        System.out.println("=".repeat(60));

        System.out.println("\nCustom Script Examples:");
        System.out.println("=".repeat(60));
        System.out.println("Phone masking:");
        System.out.println("  return MaskingFunctions.maskPhone(value)");
        System.out.println("\nEmail domain replacement:");
        System.out.println("  return MaskingFunctions.maskEmailDomain(value, 'example.com')");
        System.out.println("\nCustom pattern:");
        System.out.println("  return value.replaceAll('(\\\\d{3})\\\\d{4}(\\\\d{4})', '\$1****\$2')");
        System.out.println("=".repeat(60));

        return 0;
    }

    @CommandLine.Command(name = "test-filter", description = "Test row filter condition")
    public Integer testFilter(
            @CommandLine.Option(names = {"--condition"}, description = "Filter condition (e.g., age < 18)", required = true)
            String condition,
            @CommandLine.Option(names = {"--data"}, description = "JSON data for testing", required = true)
            String jsonData
    ) {
        try {
            DataMaskingConfig config = new DataMaskingConfig();
            config.setEnabled(true);

            MaskingRuleEngine engine = new MaskingRuleEngine(config, null);

            Map<String, Object> rowData = parseJsonData(jsonData);

            System.out.println("Testing Filter Condition:");
            System.out.println("=".repeat(60));
            System.out.println("Condition: " + condition);
            System.out.println("Data: " + rowData);
            System.out.println("-".repeat(60));

            com.cdc.common.config.DataMaskingConfig.RowFilterRule filter =
                    new com.cdc.common.config.DataMaskingConfig.RowFilterRule();
            filter.setCondition(condition);
            filter.setTableName("test.table");

            boolean result = engine.testFilter(filter, rowData);

            System.out.println("Result: " + (result ? "MATCH" : "NO MATCH"));
            System.out.println("=".repeat(60));

            engine.shutdown();

            return 0;

        } catch (Exception e) {
            logger.error("Filter test failed", e);
            System.err.println("Error: " + e.getMessage());
            return 2;
        }
    }

    private Map<String, Object> parseJsonData(String json) {
        Map<String, Object> result = new HashMap<>();

        try {
            com.fasterxml.jackson.databind.ObjectMapper mapper = new com.fasterxml.jackson.databind.ObjectMapper();
            result = mapper.readValue(json,
                    mapper.getTypeFactory().constructMapType(HashMap.class, String.class, Object.class));
        } catch (Exception e) {
            System.err.println("Failed to parse JSON data, using as simple key=value: " + e.getMessage());
            for (String pair : json.split(",")) {
                String[] kv = pair.split("=", 2);
                if (kv.length == 2) {
                    String key = kv[0].trim();
                    String val = kv[1].trim();
                    try {
                        result.put(key, Integer.parseInt(val));
                    } catch (NumberFormatException e1) {
                        result.put(key, val);
                    }
                }
            }
        }

        return result;
    }
}
