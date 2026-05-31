package com.cdc.cli.command;

import com.cdc.cli.model.ValidationConfig;
import com.cdc.cli.model.ValidationResult;
import com.cdc.cli.validator.RowCountValidator;
import com.cdc.cli.validator.SampleDataValidator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import picocli.CommandLine;

import java.util.concurrent.Callable;

@CommandLine.Command(
        name = "validate",
        description = "Validate data consistency between source and target databases",
        mixinStandardHelpOptions = true
)
public class ValidateCommand implements Callable<Integer> {

    private static final Logger logger = LoggerFactory.getLogger(ValidateCommand.class);

    @CommandLine.Option(names = {"--source-type"}, required = true, description = "Source database type: mysql or postgresql")
    private String sourceType;

    @CommandLine.Option(names = {"--source-host"}, required = true, description = "Source database host")
    private String sourceHost;

    @CommandLine.Option(names = {"--source-port"}, description = "Source database port", defaultValue = "3306")
    private int sourcePort;

    @CommandLine.Option(names = {"--source-database"}, required = true, description = "Source database name")
    private String sourceDatabase;

    @CommandLine.Option(names = {"--source-username"}, required = true, description = "Source database username")
    private String sourceUsername;

    @CommandLine.Option(names = {"--source-password"}, required = true, description = "Source database password")
    private String sourcePassword;

    @CommandLine.Option(names = {"--target-type"}, required = true, description = "Target database type: mysql or postgresql")
    private String targetType;

    @CommandLine.Option(names = {"--target-host"}, required = true, description = "Target database host")
    private String targetHost;

    @CommandLine.Option(names = {"--target-port"}, description = "Target database port", defaultValue = "3306")
    private int targetPort;

    @CommandLine.Option(names = {"--target-database"}, required = true, description = "Target database name")
    private String targetDatabase;

    @CommandLine.Option(names = {"--target-username"}, required = true, description = "Target database username")
    private String targetUsername;

    @CommandLine.Option(names = {"--target-password"}, required = true, description = "Target database password")
    private String targetPassword;

    @CommandLine.Option(names = {"--table"}, required = true, description = "Table name (e.g., schema.table or table)")
    private String tableName;

    @CommandLine.Option(names = {"--columns"}, description = "Columns to include (comma-separated)")
    private String columns;

    @CommandLine.Option(names = {"--sample-size"}, description = "Number of rows to sample", defaultValue = "1000")
    private int sampleSize;

    @CommandLine.Option(names = {"--check-row-count"}, description = "Check row count", defaultValue = "true")
    private boolean checkRowCount;

    @CommandLine.Option(names = {"--check-sample-data"}, description = "Check sample data", defaultValue = "true")
    private boolean checkSampleData;

    @CommandLine.Option(names = {"--output-format"}, description = "Output format: text or json", defaultValue = "text")
    private String outputFormat;

    @Override
    public Integer call() {
        try {
            ValidationConfig config = buildConfig();

            System.out.println("=" .repeat(80));
            System.out.println("CDC Data Consistency Validation");
            System.out.println("=" .repeat(80));
            System.out.printf("Source: %s://%s:%d/%s%n", sourceType, sourceHost, sourcePort, sourceDatabase);
            System.out.printf("Target: %s://%s:%d/%s%n", targetType, targetHost, targetPort, targetDatabase);
            System.out.printf("Table: %s%n", tableName);
            System.out.println("-" .repeat(80));

            boolean overallPassed = true;

            if (checkRowCount) {
                System.out.println("\n[1/2] Running Row Count Validation...");
                RowCountValidator rowCountValidator = new RowCountValidator();
                ValidationResult rowCountResult = rowCountValidator.validate(config);
                printResult(rowCountResult, "Row Count");
                if (!rowCountResult.isPassed()) {
                    overallPassed = false;
                }
            }

            if (checkSampleData) {
                System.out.println("\n[2/2] Running Sample Data Validation...");
                SampleDataValidator sampleValidator = new SampleDataValidator();
                ValidationResult sampleResult = sampleValidator.validate(config);
                printResult(sampleResult, "Sample Data");
                if (!sampleResult.isPassed()) {
                    overallPassed = false;
                }
            }

            System.out.println("\n" + "=" .repeat(80));
            System.out.printf("OVERALL RESULT: %s%n", overallPassed ? "PASSED" : "FAILED");
            System.out.println("=" .repeat(80));

            return overallPassed ? 0 : 1;

        } catch (Exception e) {
            logger.error("Validation failed", e);
            System.err.println("Error: " + e.getMessage());
            return 2;
        }
    }

    private ValidationConfig buildConfig() {
        ValidationConfig config = new ValidationConfig();
        config.setSourceType(sourceType);
        config.setSourceHost(sourceHost);
        config.setSourcePort(sourcePort);
        config.setSourceDatabase(sourceDatabase);
        config.setSourceUsername(sourceUsername);
        config.setSourcePassword(sourcePassword);
        config.setTargetType(targetType);
        config.setTargetHost(targetHost);
        config.setTargetPort(targetPort);
        config.setTargetDatabase(targetDatabase);
        config.setTargetUsername(targetUsername);
        config.setTargetPassword(targetPassword);
        config.setTableName(tableName);
        config.setSampleSize(sampleSize);

        if (columns != null && !columns.isEmpty()) {
            config.setIncludedColumns(columns.split(","));
        }

        return config;
    }

    private void printResult(ValidationResult result, String validationType) {
        System.out.println("\n" + validationType + " Validation Result:");
        System.out.println("  Status: " + (result.isPassed() ? "PASSED" : "FAILED"));
        System.out.println("  Table: " + result.getTableName());

        if (result.getSourceRowCount() > 0 || result.getTargetRowCount() > 0) {
            System.out.println("  Source Row Count: " + result.getSourceRowCount());
            System.out.println("  Target Row Count: " + result.getTargetRowCount());
            System.out.println("  Row Count Diff: " + result.getRowCountDiff());
        }

        if (result.getSampleSize() > 0) {
            System.out.println("  Samples Checked: " + result.getSampleSize());
            System.out.println("  Mismatches Found: " + result.getMismatchedSamples());
        }

        System.out.println("  Duration: " + result.getDurationMs() + "ms");

        if (result.getErrorMessage() != null) {
            System.out.println("  Error: " + result.getErrorMessage());
        }

        if (!result.getMismatches().isEmpty()) {
            System.out.println("\n  Top Mismatches:");
            int maxShow = Math.min(10, result.getMismatches().size());
            for (int i = 0; i < maxShow; i++) {
                System.out.println("    " + result.getMismatches().get(i));
            }
            if (result.getMismatches().size() > maxShow) {
                System.out.println("    ... and " + (result.getMismatches().size() - maxShow) + " more");
            }
        }
    }
}
