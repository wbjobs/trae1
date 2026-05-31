package cli

import (
	"fmt"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/pkg/config"
	"github.com/spf13/cobra"
)

var applyCmd = &cobra.Command{
	Use:   "apply [config-file]",
	Short: "批量应用YAML配置文件",
	Long: `从YAML配置文件中批量应用IO限制到多个容器。
支持设置默认值，自动合并到各个容器规则中。
应用前会自动保存回滚点，支持随时回滚。`,
	Example: `  # 应用配置文件
  io-qos apply config.yaml

  # 试运行，不实际应用
  io-qos apply config.yaml --dry-run

  # 生成配置模板
  io-qos apply --generate template.yaml`,
	Args: cobra.MaximumNArgs(1),
	RunE: runApply,
}

func init() {
	rootCmd.AddCommand(applyCmd)

	applyCmd.Flags().Bool("dry-run", false, "只显示计划的操作，不实际应用")
	applyCmd.Flags().String("generate", "", "生成配置模板到指定文件")
}

func runApply(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	verbose, _ := cmd.Flags().GetBool("verbose")
	dryRun, _ := cmd.Flags().GetBool("dry-run")
	generate, _ := cmd.Flags().GetString("generate")

	mgr := config.NewManager(cgroupRoot)

	if generate != "" {
		if err := mgr.GenerateTemplate(generate); err != nil {
			return err
		}
		fmt.Printf("配置模板已生成: %s\n", generate)
		return nil
	}

	if len(args) == 0 {
		return fmt.Errorf("请指定配置文件路径，或使用 --generate 生成模板")
	}

	configPath := args[0]
	cfg, err := mgr.LoadConfig(configPath)
	if err != nil {
		return err
	}

	fmt.Printf("加载配置文件: %s (版本: %s)\n", configPath, cfg.Version)
	fmt.Printf("包含 %d 条规则\n", len(cfg.Rules))
	fmt.Println()

	if cfg.Defaults != nil && verbose {
		cyan := color.New(color.FgCyan).SprintFunc()
		fmt.Printf("%s:\n", cyan("默认限制"))
		fmt.Printf("  读带宽: %s\n", formatBytesDefault(cfg.Defaults.ReadBPS))
		fmt.Printf("  写带宽: %s\n", formatBytesDefault(cfg.Defaults.WriteBPS))
		fmt.Printf("  读IOPS: %d\n", cfg.Defaults.ReadIOPS)
		fmt.Printf("  写IOPS: %d\n", cfg.Defaults.WriteIOPS)
		fmt.Printf("  优先级: %s\n", cfg.Defaults.Priority)
		fmt.Println()
	}

	green := color.New(color.FgGreen).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	cyan := color.New(color.FgCyan).SprintFunc()

	fmt.Printf("%s:\n", cyan("即将应用的规则"))
	for i, rule := range cfg.Rules {
		fmt.Printf("  [%d] 容器: %s\n", i+1, rule.ContainerID)
		if rule.CgroupPath != "" {
			fmt.Printf("      Cgroup路径: %s\n", rule.CgroupPath)
		}
		fmt.Printf("      限制: 读=%s, 写=%s, 读IOPS=%d, 写IOPS=%d, 优先级=%s\n",
			formatBytesDefault(rule.Limits.ReadBPS),
			formatBytesDefault(rule.Limits.WriteBPS),
			rule.Limits.ReadIOPS,
			rule.Limits.WriteIOPS,
			rule.Limits.Priority)
	}
	fmt.Println()

	if dryRun {
		fmt.Println(yellow("(dry-run模式，未实际应用任何更改)"))
		return nil
	}

	rollback, err := mgr.ApplyConfig(cfg, false)
	if err != nil {
		if rollback != nil && len(rollback.AppliedRules) > 0 {
			fmt.Printf("%s: 部分应用失败，已创建回滚点\n", yellow("警告"))
			fmt.Printf("回滚点时间: %s\n", rollback.Timestamp.Format("2006-01-02 15:04:05"))
			fmt.Printf("已应用 %d 条规则\n", len(rollback.AppliedRules))
			fmt.Println("使用 'io-qos rollback' 可回滚")
		}
		return err
	}

	fmt.Printf("%s! 成功应用 %d 条规则\n", green("完成"), len(rollback.AppliedRules))
	fmt.Printf("回滚点已保存: %s\n", rollback.Timestamp.Format("2006-01-02 15:04:05"))
	fmt.Println("使用 'io-qos rollback' 可回滚到此状态")

	return nil
}

func formatBytesDefault(val int64) string {
	if val == 0 {
		return "无限制"
	}
	return formatBytes(uint64(val)) + "/s"
}
