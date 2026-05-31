package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import com.configcenter.proto.ConfigEntry;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.Callable;

@Command(name = "publish", description = "Publish a YAML config")
public class PublishCmd implements Callable<Integer> {

    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true, description = "dev/staging/prod")
    private String profile;

    @Option(names = {"-k", "--key"}, defaultValue = "")
    private String key;

    @Option(names = {"-f", "--file"}, description = "YAML file path")
    private String file;

    @Option(names = {"-c", "--content"}, description = "YAML content inline")
    private String content;

    @Option(names = {"-o", "--operator"}, defaultValue = "cli")
    private String operator;

    @Override
    public Integer call() throws Exception {
        String yaml;
        if (file != null && !file.isEmpty()) {
            yaml = Files.readString(Path.of(file));
        } else if (content != null && !content.isEmpty()) {
            yaml = content;
        } else {
            System.err.println("either --file or --content is required");
            return 1;
        }

        try (ConfigCenterClient c = parent.client()) {
            ConfigEntry e = c.publish(application, profile, key, yaml, operator);
            System.out.printf("OK version=%d updatedAt=%d updatedBy=%s%n",
                    e.getVersion(), e.getUpdatedAt(), e.getUpdatedBy());
        }
        return 0;
    }
}
