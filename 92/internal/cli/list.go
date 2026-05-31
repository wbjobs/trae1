package cli

import (
	"fmt"
	"strings"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/spf13/cobra"
)

var listCmd = &cobra.Command{
	Use:   "list",
	Short: "列出所有可发现的容器",
	Long: `列出系统中所有可发现的容器，包括通过docker和cgroupfs发现的容器。
显示容器ID、名称、cgroup路径和运行状态。`,
	Example: `  # 列出所有容器
  io-qos list

  # 只显示docker容器
  io-qos list --runtime docker

  # 详细输出
  io-qos list -v`,
	RunE: runList,
}

func init() {
	rootCmd.AddCommand(listCmd)

	listCmd.Flags().String("runtime", "", "按运行时过滤 (docker, cgroup)")
}

func runList(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")
	runtimeFilter, _ := cmd.Flags().GetString("runtime")

	discoverer := container.NewDiscovererWithRoot(cgroupRoot)
	containers, err := discoverer.DiscoverAll()
	if err != nil {
		return err
	}

	if runtimeFilter != "" {
		var filtered []container.ContainerInfo
		for _, c := range containers {
			if c.Runtime == runtimeFilter {
				filtered = append(filtered, c)
			}
		}
		containers = filtered
	}

	fmt.Printf("发现 %d 个容器:\n", len(containers))
	fmt.Println()

	cyan := color.New(color.FgCyan, color.Bold).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	white := color.New(color.FgWhite, color.Bold).SprintFunc()

	fmt.Println(strings.Repeat("=", 120))
	fmt.Printf(
		"%-12s %-20s %-10s %-10s %s\n",
		white("ID"),
		white("名称"),
		white("运行时"),
		white("状态"),
		white("Cgroup路径"),
	)
	fmt.Println(strings.Repeat("-", 120))

	for _, c := range containers {
		id := c.ID
		if len(id) > 12 {
			id = id[:12]
		}

		name := c.Name
		if len(name) > 18 {
			name = name[:15] + "..."
		}

		runtime := c.Runtime
		if runtime == "docker" {
			runtime = cyan(runtime)
		} else if runtime == "cgroup" {
			runtime = yellow(runtime)
		}

		state := c.State
		if state == "running" {
			state = green(state)
		} else if state != "" {
			state = yellow(state)
		}

		fmt.Printf(
			"%-12s %-20s %-10s %-10s %s\n",
			id,
			name,
			runtime,
			state,
			c.CgroupPath,
		)

		if verbose && len(c.Labels) > 0 {
			fmt.Println("  标签:")
			for k, v := range c.Labels {
				fmt.Printf("    %s: %s\n", k, v)
			}
		}
	}

	fmt.Println(strings.Repeat("=", 120))

	return nil
}
