package cli

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"
)

var (
	flagVerbose bool
	flagDaemonPort int
	flagDataDir   string
)

var rootCmd = &cobra.Command{
	Use:   "lxc-migrate",
	Short: "LXC container live migration CLI tool",
	Long: `LXC-Migrate is a command-line tool for live-migrating running LXC containers
between physical hosts with minimal service interruption (<100ms).

It uses CRIU (Checkpoint/Restore In Userspace) to capture the full process state
including memory pages, file descriptors, and network connections, then streams
the checkpoint data to the target host over TCP for restore.`,
	SilenceUsage:  true,
	SilenceErrors: true,
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %v\n", err)
		os.Exit(1)
	}
}

func init() {
	rootCmd.PersistentFlags().BoolVarP(&flagVerbose, "verbose", "v", false, "enable verbose output")
}
