package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.HistoryEntry;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.time.Instant;
import java.util.List;
import java.util.concurrent.Callable;

@Command(name = "history", description = "Show config history")
public class HistoryCmd implements Callable<Integer> {

    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true)
    private String profile;

    @Option(names = {"-k", "--key"}, defaultValue = "")
    private String key;

    @Option(names = {"-n", "--limit"}, defaultValue = "20")
    private int limit;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            List<HistoryEntry> list = c.getHistory(application, profile, key, limit);
            System.out.printf("%-8s %-10s %-12s %-24s %s%n",
                    "VERSION", "TYPE", "OPERATOR", "CHANGED_AT", "DIFF");
            for (HistoryEntry e : list) {
                System.out.printf("%-8d %-10s %-12s %-24s %s%n",
                        e.getVersion(),
                        e.getChangeType(),
                        e.getOperator(),
                        Instant.ofEpochMilli(e.getChangedAt()).toString(),
                        e.getDiff());
            }
            System.out.println("Total: " + list.size());
        }
        return 0;
    }
}
