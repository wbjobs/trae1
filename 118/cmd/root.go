package cmd

import (
	"github.com/spf13/cobra"
)

var rootCmd = &cobra.Command{
	Use:   "lxc-migrate",
	Short: "LXC容器热迁移工具 - 支持不停机迁移",
	Long: `lxc-migrate 是一个高性能的LXC容器热迁移工具，基于CRIU技术实现
运行中容器的跨主机迁移，服务中断时间<100ms。

主要功能：
- 基于CRIU的容器checkpoint/restore
- TCP流式传输容器状态
- 预拷贝(pre-copy)模式减少停机时间
- 带宽限制(--bwlimit)
- 实时迁移进度显示
- 迁移前资源检查
- 自动网络配置更新`,
}

func Execute() error {
	return rootCmd.Execute()
}

func init() {
	rootCmd.AddCommand(migrateCmd)
	rootCmd.AddCommand(daemonCmd)
	rootCmd.AddCommand(checkCmd)
}
