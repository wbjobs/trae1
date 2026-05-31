package com.dbagent.masking;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;
import java.util.Scanner;

public class MaskingValidator {

    private static final String SEPARATOR = "========================================================";
    private static final String SUB_SEPARATOR = "--------------------------------------------------------";

    public static void main(String[] args) {
        String configPath = null;

        for (int i = 0; i < args.length; i++) {
            switch (args[i]) {
                case "--config":
                case "-c":
                    if (i + 1 < args.length) {
                        configPath = args[i + 1];
                    }
                    break;
                case "--sql":
                case "-s":
                    if (i + 1 < args.length) {
                        runOnce(configPath, args[i + 1]);
                        return;
                    }
                    break;
                case "--file":
                case "-f":
                    if (i + 1 < args.length) {
                        runFromFile(configPath, args[i + 1]);
                        return;
                    }
                    break;
                case "--list":
                case "-l":
                    listRules(configPath);
                    return;
                case "--help":
                case "-h":
                    printHelp();
                    return;
                case "--interactive":
                case "-i":
                    runInteractive(configPath);
                    return;
                default:
                    break;
            }
        }

        runInteractive(configPath);
    }

    private static void runOnce(String configPath, String sql) {
        printHeader();
        SensitiveDataMasker masker = getMasker(configPath);

        System.out.println("Input SQL:");
        System.out.println("  " + sql);
        System.out.println();

        String masked = masker.mask(sql);
        System.out.println("Masked SQL:");
        System.out.println("  " + masked);
        System.out.println();
        printMaskedSummary(sql, masked);
    }

    private static void runFromFile(String configPath, String filePath) {
        printHeader();
        try {
            String content = new String(Files.readAllBytes(Paths.get(filePath)), StandardCharsets.UTF_8);
            String[] lines = content.split("\\r?\\n");

            SensitiveDataMasker masker = getMasker(configPath);

            int lineNum = 0;
            for (String line : lines) {
                lineNum++;
                if (line.trim().isEmpty()) continue;

                System.out.println(SUB_SEPARATOR);
                System.out.println("Line " + lineNum + ":");
                System.out.println("  Input:  " + line);
                String masked = masker.mask(line);
                System.out.println("  Output: " + masked);
                if (!line.equals(masked)) {
                    System.out.println("  Status: MASKED");
                } else {
                    System.out.println("  Status: UNCHANGED");
                }
            }
            System.out.println(SUB_SEPARATOR);
        } catch (Exception e) {
            System.err.println("Error reading file: " + e.getMessage());
        }
    }

    private static void runInteractive(String configPath) {
        printHeader();
        SensitiveDataMasker masker = getMasker(configPath);

        System.out.println("Interactive Mode: Enter SQL to test masking (type 'exit' to quit, 'reload' to reload config)");
        System.out.println(SUB_SEPARATOR);

        Scanner scanner = new Scanner(new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8)));
        while (true) {
            System.out.print("> ");
            String input = scanner.nextLine();
            if (input == null) break;

            String trimmed = input.trim();
            if (trimmed.isEmpty()) continue;

            if ("exit".equalsIgnoreCase(trimmed) || "quit".equalsIgnoreCase(trimmed)) {
                System.out.println("Bye!");
                break;
            }

            if ("reload".equalsIgnoreCase(trimmed)) {
                if (configPath != null) {
                    MaskingConfig.getInstance(configPath).reload();
                    masker = MaskingConfig.getInstance(configPath).getMasker();
                    System.out.println("Config reloaded.");
                } else {
                    System.out.println("No config path specified, using built-in rules.");
                }
                continue;
            }

            if ("list".equalsIgnoreCase(trimmed) || "rules".equalsIgnoreCase(trimmed)) {
                printRules(masker);
                continue;
            }

            String masked = masker.mask(input);
            System.out.println("  Result: " + masked);
            if (!input.equals(masked)) {
                System.out.println("  [MASKED] Sensitive data was masked.");
            } else {
                System.out.println("  [OK] No sensitive data detected.");
            }
            System.out.println();
        }
        scanner.close();
    }

    private static void listRules(String configPath) {
        printHeader();
        SensitiveDataMasker masker = getMasker(configPath);
        printRules(masker);
    }

    private static void printRules(SensitiveDataMasker masker) {
        List<SensitiveDataMasker.MaskingRule> rules = masker.getRules();
        System.out.println("Active Masking Rules (" + rules.size() + "):");
        System.out.println(SUB_SEPARATOR);
        for (int i = 0; i < rules.size(); i++) {
            SensitiveDataMasker.MaskingRule rule = rules.get(i);
            System.out.println("Rule #" + (i + 1) + ": " + rule.getName());
            if (rule.getDescription() != null) {
                System.out.println("  Description: " + rule.getDescription());
            }
            if (rule.getFieldPattern() != null) {
                System.out.println("  Field Pattern: " + rule.getFieldPattern());
            }
            if (rule.getSqlPattern() != null) {
                System.out.println("  SQL Pattern:   " + rule.getSqlPattern());
            }
            if (rule.getValuePattern() != null) {
                System.out.println("  Value Pattern: " + rule.getValuePattern());
            }
            if (rule.getTargetField() != null) {
                System.out.println("  Target Field:  " + rule.getTargetField());
            }
            System.out.println("  Enabled:       " + (rule.isEnabled() ? "yes" : "no"));
            if (i < rules.size() - 1) {
                System.out.println();
            }
        }
    }

    private static SensitiveDataMasker getMasker(String configPath) {
        if (configPath != null) {
            return MaskingConfig.getInstance(configPath).getMasker();
        }
        return MaskingConfig.getInstance().getMasker();
    }

    private static void printMaskedSummary(String original, String masked) {
        if (original.equals(masked)) {
            System.out.println("Result: No sensitive data detected.");
        } else {
            System.out.println("Result: Sensitive data was successfully masked.");
            System.out.println("Changes detected: " + countCharDiff(original, masked) + " characters changed.");
        }
    }

    private static int countCharDiff(String a, String b) {
        int count = 0;
        int minLen = Math.min(a.length(), b.length());
        for (int i = 0; i < minLen; i++) {
            if (a.charAt(i) != b.charAt(i)) {
                count++;
            }
        }
        return count + Math.abs(a.length() - b.length());
    }

    private static void printHeader() {
        System.out.println();
        System.out.println(SEPARATOR);
        System.out.println("  DB Tracing Agent - Masking Rule Validator");
        System.out.println(SEPARATOR);
        System.out.println();
    }

    private static void printHelp() {
        printHeader();
        System.out.println("Usage:");
        System.out.println("  java -cp agent.jar com.dbagent.masking.MaskingValidator [options]");
        System.out.println();
        System.out.println("Options:");
        System.out.println("  -c, --config <path>    Path to masking rules JSON config file");
        System.out.println("  -s, --sql <sql>        Single SQL statement to test");
        System.out.println("  -f, --file <path>      File with SQL statements (one per line)");
        System.out.println("  -l, --list             List all active masking rules");
        System.out.println("  -i, --interactive      Enter interactive mode (default)");
        System.out.println("  -h, --help             Show this help message");
        System.out.println();
        System.out.println("Interactive commands:");
        System.out.println("  exit/quit              Exit the tool");
        System.out.println("  reload                 Reload masking config from disk");
        System.out.println("  list/rules             Show current masking rules");
        System.out.println();
        System.out.println("Examples:");
        System.out.println("  # Test a single SQL");
        System.out.println("  java -cp agent.jar com.dbagent.masking.MaskingValidator -s \"SELECT * FROM users WHERE password='secret123'\"");
        System.out.println();
        System.out.println("  # Test with custom config");
        System.out.println("  java -cp agent.jar com.dbagent.masking.MaskingValidator -c /path/to/masking-rules.json -s \"...\"");
        System.out.println();
        System.out.println("  # Interactive mode");
        System.out.println("  java -cp agent.jar com.dbagent.masking.MaskingValidator -i");
        System.out.println();
    }
}
