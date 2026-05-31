package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import picocli.CommandLine;
import picocli.CommandLine.*;

import java.util.List;
import java.util.concurrent.Callable;

@Command(name = "list", description = "List apps/profiles/keys",
        subcommands = {ListAppsCmd.class, ListProfilesCmd.class, ListKeysCmd.class})
public class ListCmd implements Callable<Integer> {
    @Override
    public Integer call() {
        CommandLine.usage(this, System.out);
        return 0;
    }
}

@Command(name = "apps", description = "List applications")
class ListAppsCmd implements Callable<Integer> {
    @ParentCommand
    private CliMain parent;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            List<String> apps = c.listApps();
            apps.forEach(System.out::println);
        }
        return 0;
    }
}

@Command(name = "profiles", description = "List profiles of an application")
class ListProfilesCmd implements Callable<Integer> {
    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            List<String> profiles = c.listProfiles(application);
            profiles.forEach(System.out::println);
        }
        return 0;
    }
}

@Command(name = "keys", description = "List keys of an application/profile")
class ListKeysCmd implements Callable<Integer> {
    @ParentCommand
    private CliMain parent;

    @Option(names = {"-a", "--application"}, required = true)
    private String application;

    @Option(names = {"-e", "--profile"}, required = true)
    private String profile;

    @Override
    public Integer call() {
        try (ConfigCenterClient c = parent.client()) {
            List<String> keys = c.listKeys(application, profile);
            keys.forEach(System.out::println);
        }
        return 0;
    }
}
