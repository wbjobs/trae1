package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.ConfigEntry;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.util.concurrent.Callable;

@Command(name = "rollback", description = "Rollback config to a target version")
public class RollbackCmd implements Callable<Integer> {

    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true)
    private String profile;

    @Option(names = {"-k", "--key"}, defaultValue = "")
    private String key;

    @Option(names = {"-v", "--version"}, required = true, description = "target version to rollback to")
    private long version;

    @Option(names = {"-o", "--operator"}, defaultValue = "cli")
    private String operator;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            ConfigEntry e = c.rollback(application, profile, key, version, operator);
            System.out.printf("OK rolled back to version %d (new version=%d)%n",
                    version, e.getVersion());
        }
        return 0;
    }
}
