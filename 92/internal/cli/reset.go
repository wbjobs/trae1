package cli

import (
	"fmt"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/spf13/cobra"
)

var resetCmd = &cobra.Command{
	Use:   "reset [container IDs...]",
	Short: "重置容器的IO限制（移除所有限制）",
	Long: `移除一个或多个容器的所有IO限制，恢复为无限制状态。
相当于将读写带宽和IOPS都设置为"max"。`,
	Example: `  # 重置单个容器的IO限制
  io-qos reset container1

  # 重置多个容器的IO限制
  io-qos reset container1 container2

  # 重置所有容器的IO限制
  io-qos reset --all`,
	RunE: runReset,
}

func init() {
	rootCmd.AddCommand(resetCmd)

	resetCmd.Flags().Bool("all", false, "重置所有可发现容器的IO限制")
	resetCmd.Flags().Bool("dry-run", false, "只显示计划的操作，不实际应用")
}

func runReset(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")
	all, _ := cmd.Flags().GetBool("all")
	dryRun, _ := cmd.Flags().GetBool("dry-run")

	discoverer := container.NewDiscovererWithRoot(cgroupRoot)
	ioCtl := cgroup.NewIOControllerWithRoot(cgroupRoot)

	var containers []container.ContainerInfo
	var err error

	if all {
		containers, err = discoverer.DiscoverAll()
	} else {
		if len(args) == 0 {
			return fmt.Errorf("请指定容器ID，或使用 --all 重置所有容器")
		}
		containers, err = discoverer.Discover(args)
	}

	if err != nil {
		return err
	}

	if len(containers) == 0 {
		return fmt.Errorf("没有找到可重置的容器")
	}

	yellow := color.New(color.FgYellow).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()

	fmt.Printf("将重置 %d 个容器的IO限制:\n", len(containers))
	fmt.Println()

	unlimited := types.IOLimit{
		ReadBPS:   0,
		WriteBPS:  0,
		ReadIOPS:  0,
		WriteIOPS: 0,
		Priority:  types.PriorityMedium,
	}

	devices, _ := ioCtl.GetBlockDevices()

	for _, c := range containers {
		fmt.Printf("容器: %s (%s)\n", c.Name, c.ID[:12])
		fmt.Printf("  Cgroup路径: %s\n", c.CgroupPath)

		if verbose {
			oldLimits, err := ioCtl.GetCurrentLimits(c.CgroupPath)
			if err == nil {
				fmt.Printf("  原限制: 读=%s, 写=%s, 读IOPS=%d, 写IOPS=%d\n",
					formatBytesDefault(oldLimits.ReadBPS),
					formatBytesDefault(oldLimits.WriteBPS),
					oldLimits.ReadIOPS,
					oldLimits.WriteIOPS)
			}
		}

		if dryRun {
			fmt.Printf("  操作: %s (dry-run模式)\n", yellow("跳过"))
			continue
		}

		for _, dev := range devices {
			fullPath := fmt.Sprintf("%s/%s/io.max", cgroupRoot, c.CgroupPath)
			if err := ioCtl.SetIOLimit(c.CgroupPath, dev, unlimited); err != nil {
				fmt.Printf("  失败: %v (文件: %s)\n", err, fullPath)
				continue
			}

			weightPath := fmt.Sprintf("%s/%s/io.weight", cgroupRoot, c.CgroupPath)
			if err := ioCtl.SetIOPriority(c.CgroupPath, dev, types.PriorityMedium); err != nil {
				fmt.Printf("  优先级重置失败: %v (文件: %s)\n", err, weightPath)
				continue
			}
		}

		fmt.Printf("  结果: %s\n", green("已重置为无限制"))
	}

	if dryRun {
		fmt.Println()
		fmt.Println(yellow("(dry-run模式，未实际应用任何更改)"))
	}

	return nil
}
