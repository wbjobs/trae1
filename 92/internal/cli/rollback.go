package cli

import (
	"fmt"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/pkg/config"
	"github.com/spf13/cobra"
)

var rollbackCmd = &cobra.Command{
	Use:   "rollback",
	Short: "回滚到上一个配置状态",
	Long: `回滚到上一个应用配置时保存的状态。
每次使用 'apply' 或 'set' 命令时都会自动保存回滚点。
可以使用 --list 查看所有可用的回滚点。`,
	Example: `  # 回滚到上一个状态
  io-qos rollback

  # 列出所有回滚点
  io-qos rollback --list

  # 回滚但不实际应用
  io-qos rollback --dry-run`,
	RunE: runRollback,
}

func init() {
	rootCmd.AddCommand(rollbackCmd)

	rollbackCmd.Flags().Bool("list", false, "列出所有可用的回滚点")
	rollbackCmd.Flags().Bool("dry-run", false, "只显示将要回滚的内容，不实际应用")
}

func runRollback(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")
	list, _ := cmd.Flags().GetBool("list")
	dryRun, _ := cmd.Flags().GetBool("dry-run")

	mgr := config.NewManager(cgroupRoot)

	if list {
		points, err := mgr.ListRollbackPoints()
		if err != nil {
			return fmt.Errorf("无法获取回滚点列表: %w", err)
		}

		if len(points) == 0 {
			fmt.Println("没有找到回滚点")
			return nil
		}

		fmt.Printf("找到 %d 个回滚点:\n", len(points))
		fmt.Println()

		cyan := color.New(color.FgCyan, color.Bold).SprintFunc()
		white := color.New(color.FgWhite, color.Bold).SprintFunc()

		for i, rb := range points {
			fmt.Printf("%s %d\n", cyan("回滚点"), i+1)
			fmt.Printf("  %s: %s\n", white("时间"), rb.Timestamp.Format("2006-01-02 15:04:05"))
			fmt.Printf("  %s: %d 个容器\n", white("影响容器"), len(rb.AppliedRules))

			if verbose {
				fmt.Printf("  %s:\n", white("应用规则"))
				for _, rule := range rb.AppliedRules {
					fmt.Printf("    - %s: 读=%d/s, 写=%d/s, 读IOPS=%d, 写IOPS=%d\n",
						rule.ContainerID,
						rule.Limits.ReadBPS,
						rule.Limits.WriteBPS,
						rule.Limits.ReadIOPS,
						rule.Limits.WriteIOPS)
				}
			}
			fmt.Println()
		}

		return nil
	}

	yellow := color.New(color.FgYellow).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()

	if dryRun {
		rb, err := mgr.ListRollbackPoints()
		if err != nil || len(rb) == 0 {
			return fmt.Errorf("没有找到回滚点")
		}

		latest := rb[len(rb)-1]
		fmt.Printf("%s (dry-run模式)\n", yellow("回滚预览"))
		fmt.Printf("回滚点时间: %s\n", latest.Timestamp.Format("2006-01-02 15:04:05"))
		fmt.Printf("将回滚 %d 个容器:\n", len(latest.PreviousState))

		for containerID, oldLimits := range latest.PreviousState {
			fmt.Printf("  - %s: 读=%s, 写=%s, 读IOPS=%d, 写IOPS=%d, 优先级=%s\n",
				containerID[:12],
				formatBytesDefault(oldLimits.ReadBPS),
				formatBytesDefault(oldLimits.WriteBPS),
				oldLimits.ReadIOPS,
				oldLimits.WriteIOPS,
				oldLimits.Priority)
		}

		return nil
	}

	rb, err := mgr.RollbackLast()
	if err != nil {
		return fmt.Errorf("回滚失败: %w", err)
	}

	fmt.Printf("%s! 成功回滚\n", green("完成"))
	fmt.Printf("回滚点时间: %s\n", rb.Timestamp.Format("2006-01-02 15:04:05"))
	fmt.Printf("已回滚 %d 个容器\n", len(rb.PreviousState))

	return nil
}
