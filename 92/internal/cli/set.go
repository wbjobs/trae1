package cli

import (
	"fmt"

	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/spf13/cobra"
)

var setCmd = &cobra.Command{
	Use:   "set [container IDs...]",
	Short: "为容器设置IO限制",
	Long: `为一个或多个容器设置磁盘IO限制，包括读写带宽、IOPS上限和优先级。
支持同时对多个容器设置相同的限制。`,
	Example: `  # 设置单个容器的写带宽限制为10MB/s
  io-qos set container1 --write-bps 10MB

  # 设置多个容器的读写IOPS限制和优先级
  io-qos set container1 container2 --read-iops 1000 --write-iops 500 --priority high

  # 通过cgroup路径设置
  io-qos set /sys/fs/cgroup/docker/abc123 --read-bps 100MB`,
	Args: cobra.MinimumNArgs(1),
	RunE: runSet,
}

func init() {
	rootCmd.AddCommand(setCmd)

	setCmd.Flags().String("read-bps", "", "读带宽上限 (如: 10MB, 1GB)")
	setCmd.Flags().String("write-bps", "", "写带宽上限 (如: 10MB, 1GB)")
	setCmd.Flags().Int64("read-iops", 0, "读IOPS上限")
	setCmd.Flags().Int64("write-iops", 0, "写IOPS上限")
	setCmd.Flags().String("priority", "", "IO优先级 (high/medium/low)")
	setCmd.Flags().Bool("dry-run", false, "只显示计划的操作，不实际应用")
}

func runSet(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")
	dryRun, _ := cmd.Flags().GetBool("dry-run")

	readBPS, _ := cmd.Flags().GetString("read-bps")
	writeBPS, _ := cmd.Flags().GetString("write-bps")
	readIOPS, _ := cmd.Flags().GetInt64("read-iops")
	writeIOPS, _ := cmd.Flags().GetInt64("write-iops")
	priority, _ := cmd.Flags().GetString("priority")

	if priority != "" && priority != types.PriorityHigh &&
		priority != types.PriorityMedium && priority != types.PriorityLow {
		return fmt.Errorf("invalid priority: %s (must be high/medium/low)", priority)
	}

	readBPSVal, err := types.ParseSize(readBPS)
	if err != nil {
		return fmt.Errorf("invalid read-bps: %w", err)
	}

	writeBPSVal, err := types.ParseSize(writeBPS)
	if err != nil {
		return fmt.Errorf("invalid write-bps: %w", err)
	}

	limits := types.IOLimit{
		ReadBPS:   readBPSVal,
		WriteBPS:  writeBPSVal,
		ReadIOPS:  readIOPS,
		WriteIOPS: writeIOPS,
		Priority:  priority,
	}

	if limits.ReadBPS == 0 && limits.WriteBPS == 0 &&
		limits.ReadIOPS == 0 && limits.WriteIOPS == 0 &&
		limits.Priority == "" {
		return fmt.Errorf("no limits specified. Use --read-bps, --write-bps, --read-iops, --write-iops, or --priority")
	}

	discoverer := container.NewDiscovererWithRoot(cgroupRoot)
	ioCtl := cgroup.NewIOControllerWithRoot(cgroupRoot)

	containers, err := discoverer.Discover(args)
	if err != nil {
		return err
	}

	fmt.Printf("将对 %d 个容器应用IO限制:\n", len(containers))
	fmt.Printf("  读带宽: %s\n", formatLimit(readBPS))
	fmt.Printf("  写带宽: %s\n", formatLimit(writeBPS))
	fmt.Printf("  读IOPS: %d\n", readIOPS)
	fmt.Printf("  写IOPS: %d\n", writeIOPS)
	fmt.Printf("  优先级: %s\n", formatLimit(priority))
	fmt.Println()

	for _, c := range containers {
		fmt.Printf("容器: %s (%s)\n", c.Name, c.ID[:12])
		fmt.Printf("  Cgroup路径: %s\n", c.CgroupPath)

		if dryRun {
			fmt.Println("  操作: 跳过 (dry-run模式)")
			continue
		}

		if verbose {
			oldLimits, err := ioCtl.GetCurrentLimits(c.CgroupPath)
			if err == nil {
				fmt.Printf("  原限制: 读=%d/s, 写=%d/s, 读IOPS=%d, 写IOPS=%d\n",
					oldLimits.ReadBPS, oldLimits.WriteBPS,
					oldLimits.ReadIOPS, oldLimits.WriteIOPS)
			}
		}

		if err := ioCtl.ApplyLimits(c.CgroupPath, limits); err != nil {
			fmt.Printf("  失败: %v\n", err)
			continue
		}

		fmt.Println("  成功应用限制")
	}

	if dryRun {
		fmt.Println("\n(dry-run模式，未实际应用任何更改)")
	}

	return nil
}

func formatLimit(val string) string {
	if val == "" {
		return "未设置"
	}
	return val
}
