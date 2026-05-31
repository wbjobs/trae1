package com.configcenter.cli;

import com.configcenter.client.ConfigCenterClient;
import picocli.CommandLine;
import picocli.CommandLine.*;

@Command(
        name = "config-center-cli",
        mixinStandardHelpOptions = true,
        version = "1.0.0",
        description = "Config Center CLI",
        subcommands = {
                PublishCmd.class,
                GetCmd.class,
                WatchCmd.class,
                HistoryCmd.class,
                RollbackCmd.class,
                ListCmd.class,
                GrayCmd.class
        }
)
public class CliMain implements Runnable {

    @Option(names = {"-s", "--server"}, description = "config center server host", defaultValue = "localhost")
    public String server = "localhost";

    @Option(names = {"-p", "--port"}, description = "gRPC port", defaultValue = "9090")
    public int port = 9090;

    public static void main(String[] args) {
        int exitCode = new CommandLine(new CliMain()).execute(args);
        System.exit(exitCode);
    }

    @Override
    public void run() {
        CommandLine.usage(this, System.out);
    }

    public ConfigCenterClient client() {
        return new ConfigCenterClient(server, port);
    }
}
