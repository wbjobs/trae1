package main

import (
	"fmt"
	"os"

	"github.com/spf13/cobra"

	"terraform-config-generator/cmd"
)

func main() {
	rootCmd := cmd.NewRootCommand()

	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}