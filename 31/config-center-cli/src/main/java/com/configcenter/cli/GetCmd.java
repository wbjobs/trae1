package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.ConfigEntry;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.util.concurrent.Callable;

@Command(name = "get", description = "Get current config")
public class GetCmd implements Callable<Integer> {

    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true)
    private String profile;

    @Option(names = {"-k", "--key"}, defaultValue = "")
    private String key;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            ConfigEntry e = c.getConfig(application, profile, key);
            System.out.println("--- version: " + e.getVersion()
                    + " updatedBy: " + e.getUpdatedBy()
                    + " updatedAt: " + e.getUpdatedAt() + " ---");
            System.out.println(e.getContent());
        }
        return 0;
    }
}
