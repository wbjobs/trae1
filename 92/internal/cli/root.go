package cli

import (
	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:   "io-qos",
	Short: "容器磁盘IO QoS控制工具",
	Long: `基于cgroups v2 io控制器的容器磁盘IO QoS控制CLI工具。
支持为容器设置读写带宽、IOPS上限和优先级，
提供实时监控、动态调整、批量配置和回滚功能。`,
	Version: "1.0.0",
}

func Execute() error {
	return rootCmd.Execute()
}

func init() {
	rootCmd.PersistentFlags().String("cgroup-root", "/sys/fs/cgroup", "cgroup根目录路径")
	rootCmd.PersistentFlags().BoolP("verbose", "v", false, "启用详细输出")
}
