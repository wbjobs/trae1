package cli

import (
	"fmt"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/spf13/cobra"
)

var showCmd = &cobra.Command{
	Use:   "show [container IDs...]",
	Short: "显示容器当前的IO限制",
	Long: `显示一个或多个容器当前的IO限制设置，包括读写带宽、IOPS上限和优先级。`,
	Example: `  # 显示单个容器的IO限制
  io-qos show container1

  # 显示多个容器的IO限制
  io-qos show container1 container2`,
	Args: cobra.MinimumNArgs(1),
	RunE: runShow,
}

func init() {
	rootCmd.AddCommand(showCmd)
}

func runShow(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")

	discoverer := container.NewDiscovererWithRoot(cgroupRoot)
	ioCtl := cgroup.NewIOControllerWithRoot(cgroupRoot)

	containers, err := discoverer.Discover(args)
	if err != nil {
		return err
	}

	cyan := color.New(color.FgCyan, color.Bold).SprintFunc()
	white := color.New(color.FgWhite, color.Bold).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()

	for _, c := range containers {
		fmt.Printf("%s: %s (%s)\n", cyan("容器"), c.Name, c.ID[:12])
		fmt.Printf("  %s: %s\n", white("Cgroup路径"), c.CgroupPath)

		limits, err := ioCtl.GetCurrentLimits(c.CgroupPath)
		if err != nil {
			fmt.Printf("  %s: %v\n", yellow("错误"), err)
			continue
		}

		devices, _ := ioCtl.GetBlockDevices()
		if len(devices) > 0 && verbose {
			fmt.Printf("  %s:\n", white("块设备"))
			for _, dev := range devices {
				fmt.Printf("    - %s (%d:%d)\n", dev.Name, dev.Major, dev.Minor)
			}
		}

		fmt.Printf("  %s:\n", white("IO限制"))
		fmt.Printf("    %-10s: %s\n", "读带宽", formatLimitValue(limits.ReadBPS, "B/s"))
		fmt.Printf("    %-10s: %s\n", "写带宽", formatLimitValue(limits.WriteBPS, "B/s"))
		fmt.Printf("    %-10s: %s\n", "读IOPS", formatLimitValue(limits.ReadIOPS, ""))
		fmt.Printf("    %-10s: %s\n", "写IOPS", formatLimitValue(limits.WriteIOPS, ""))

		if len(devices) > 0 {
			weight, err := ioCtl.GetIOPriority(c.CgroupPath, devices[0])
			if err == nil {
				priority := getPriorityFromWeight(weight)
				fmt.Printf("    %-10s: %s (权重: %d)\n", "优先级", green(priority), weight)
			}
		}

		if verbose {
			stats, err := ioCtl.GetIOStats(c.CgroupPath)
			if err == nil && len(stats) > 0 {
				fmt.Printf("  %s:\n", white("当前统计"))
				for dev, s := range stats {
					fmt.Printf("    设备 %d:%d:\n", dev.Major, dev.Minor)
					fmt.Printf("      已读字节: %s\n", formatBytes(s.ReadBytes))
					fmt.Printf("      已写字节: %s\n", formatBytes(s.WriteBytes))
					fmt.Printf("      读操作数: %.0f\n", s.ReadIOPS)
					fmt.Printf("      写操作数: %.0f\n", s.WriteIOPS)
				}
			}
		}

		fmt.Println()
	}

	return nil
}

func formatLimitValue(val int64, suffix string) string {
	if val == 0 {
		return color.New(color.FgYellow).Sprint("无限制")
	}
	if suffix == "B/s" {
		return formatBytes(uint64(val)) + "/s"
	}
	return fmt.Sprintf("%d %s", val, suffix)
}

func formatBytes(bytes uint64) string {
	units := []string{"B", "KB", "MB", "GB", "TB"}
	unit := 0
	val := float64(bytes)

	for val >= 1024 && unit < len(units)-1 {
		val /= 1024
		unit++
	}

	return fmt.Sprintf("%.2f %s", val, units[unit])
}

func getPriorityFromWeight(weight int) string {
	switch {
	case weight >= 80:
		return types.PriorityHigh
	case weight >= 30:
		return types.PriorityMedium
	default:
		return types.PriorityLow
	}
}
