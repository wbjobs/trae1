package com.cdc.cli;

import com.cdc.cli.command.MaskingTestCommand;
import com.cdc.cli.command.TableStatusCommand;
import com.cdc.cli.command.ValidateCommand;
import picocli.CommandLine;
import picocli.CommandLine.Command;

@Command(
        name = "cdc-cli",
        description = "CDC Sync Command Line Interface",
        version = "1.0.0",
        mixinStandardHelpOptions = true,
        subcommands = {
                ValidateCommand.class,
                TableStatusCommand.class,
                MaskingTestCommand.class
        }
)
public class CdcCliApplication {

    public static void main(String[] args) {
        int exitCode = new CommandLine(new CdcCliApplication()).execute(args);
        System.exit(exitCode);
    }
}
