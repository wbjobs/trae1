package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.GrayscaleBatch;
import com.configcenter.proto.GetGrayscaleStatusResponse;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.Callable;

@Command(name = "gray", description = "Grayscale release commands",
        subcommands = {
                GrayStartCmd.class,
                GrayStatusCmd.class,
                GrayPromoteCmd.class,
                GrayCancelCmd.class,
                GrayListCmd.class
        })
public class GrayCmd implements Callable<Integer> {
    @Override
    public Integer call() {
        CommandLine.usage(this, System.out);
        return 0;
    }
}

@Command(name = "start", description = "Start a grayscale release")
class GrayStartCmd implements Callable<Integer> {
    @ParentCommand private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true) private String application;
    @Option(names = {"-e", "--profile"}, required = true) private String profile;
    @Option(names = {"-k", "--key"}, defaultValue = "") private String key;
    @Option(names = {"-f", "--file"}) private String file;
    @Option(names = {"-c", "--content"}) private String content;
    @Option(names = {"-p", "--percent"}, defaultValue = "10") private int percent;
    @Option(names = {"-w", "--window"}, defaultValue = "900") private long windowSec;
    @Option(names = {"-o", "--operator"}, defaultValue = "cli") private String operator;
    @Option(names = {"--error-threshold"}, defaultValue = "20") private int errorThreshold;

    @Override
    public Integer call() throws Exception {
        String yaml;
        if (file != null && !file.isEmpty()) yaml = Files.readString(Path.of(file));
        else if (content != null && !content.isEmpty()) yaml = content;
        else { System.err.println("either --file or --content is required"); return 1; }

        try (ConfigCenterClient c = parent.client()) {
            GrayscaleBatch b = c.startGrayscale(application, profile, key, yaml,
                    percent, windowSec, operator, errorThreshold);
            System.out.printf("OK batch=%s targetVersion=%d percent=%d%% window=%ds%n",
                    b.getBatchId(), b.getTargetVersion(), b.getPercent(), b.getObserveWindowSec());
        }
        return 0;
    }
}

@Command(name = "status", description = "Show grayscale batch status")
class GrayStatusCmd implements Callable<Integer> {
    @ParentCommand private CliMain parent;
    @Option(names = {"-b", "--batch"}, required = true) private String batchId;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            GetGrayscaleStatusResponse s = c.getGrayscaleStatus(batchId);
            GrayscaleBatch b = s.getBatch();
            System.out.printf("batch=%s status=%s percent=%d%% window=%ds remaining=%ds%n",
                    b.getBatchId(), b.getStatus(), b.getPercent(),
                    b.getObserveWindowSec(), s.getRemainingSec());
            System.out.printf("  totalClients=%d grayClients=%d healthy=%d error=%d upgraded=%d errorRate=%.2f%%%n",
                    s.getTotalClients(), s.getGrayClients(), s.getHealthyClients(),
                    s.getErrorClients(), s.getUpgradedClients(), s.getErrorRatePct());
        }
        return 0;
    }
}

@Command(name = "promote", description = "Promote a grayscale batch to full rollout")
class GrayPromoteCmd implements Callable<Integer> {
    @ParentCommand private CliMain parent;
    @Option(names = {"-b", "--batch"}, required = true) private String batchId;
    @Option(names = {"-o", "--operator"}, defaultValue = "cli") private String operator;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            GrayscaleBatch b = c.promoteGrayscale(batchId, operator);
            System.out.printf("OK batch=%s promoted status=%s%n", b.getBatchId(), b.getStatus());
        }
        return 0;
    }
}

@Command(name = "cancel", description = "Cancel a grayscale batch and rollback gray clients")
class GrayCancelCmd implements Callable<Integer> {
    @ParentCommand private CliMain parent;
    @Option(names = {"-b", "--batch"}, required = true) private String batchId;
    @Option(names = {"-o", "--operator"}, defaultValue = "cli") private String operator;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            GrayscaleBatch b = c.cancelGrayscale(batchId, operator);
            System.out.printf("OK batch=%s cancelled status=%s%n", b.getBatchId(), b.getStatus());
        }
        return 0;
    }
}

@Command(name = "list", description = "List grayscale batches")
class GrayListCmd implements Callable<Integer> {
    @ParentCommand private CliMain parent;
    @Option(names = {"--all"}, description = "include non-active batches") private boolean all;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            List<GrayscaleBatch> list = c.listGrayscaleBatches(!all);
            if (list.isEmpty()) { System.out.println("(no batches)"); return 0; }
            System.out.printf("%-20s %-10s %-12s %-8s %s%n",
                    "BATCH", "STATUS", "OPERATOR", "PERCENT", "TARGET_V");
            for (GrayscaleBatch b : list) {
                System.out.printf("%-20s %-10s %-12s %-6d%% v%d%n",
                        b.getBatchId(), b.getStatus(), b.getOperator(),
                        b.getPercent(), b.getTargetVersion());
            }
        }
        return 0;
    }
}
