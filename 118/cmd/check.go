package cmd

import (
	"fmt"

	"github.com/lxc-migrate/lxc-migrate/pkg/gpu"
	"github.com/lxc-migrate/lxc-migrate/pkg/lxc"
	"github.com/lxc-migrate/lxc-migrate/pkg/resource"
	"github.com/spf13/cobra"
)

var (
	checkHost  string
	checkPort  int
	checkGPU   bool
)

var checkCmd = &cobra.Command{
	Use:   "check [container]",
	Short: "检查容器和目标主机资源",
	Long: `检查容器状态和目标主机资源是否满足迁移要求。
支持GPU（NVIDIA CUDA）检测。
如果指定container，则检查本地容器；如果指定--host，则检查远程主机。`,
	Args: cobra.MaximumNArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		fmt.Println("=== 资源检查工具 ===\n")

		if len(args) == 1 {
			containerName = args[0]
			fmt.Printf("检查本地容器: %s\n\n", containerName)

			fmt.Println("容器状态:")
			if err := lxc.CheckContainerExists(containerName); err != nil {
				return fmt.Errorf("容器不存在: %w", err)
			}
			fmt.Println("  ✓ 容器存在")

			if err := lxc.CheckContainerRunning(containerName); err != nil {
				return fmt.Errorf("容器未运行: %w", err)
			}
			fmt.Println("  ✓ 容器运行中")

			info, err := lxc.GetContainerInfo(containerName)
			if err != nil {
				return fmt.Errorf("获取容器信息失败: %w", err)
			}
			fmt.Printf("\n容器信息:\n")
			fmt.Printf("  PID: %d\n", info.PID)
			fmt.Printf("  内存使用: %s\n", formatBytes(info.MemoryUsage))
			fmt.Printf("  CPU使用: %.2f%%\n", info.CPUUsage)
			fmt.Printf("  网络配置: %v\n", info.Networks)
		}

		if checkGPU {
			fmt.Println("\nGPU检测:")
			gpuState, err := gpu.DetectGPU()
			if err != nil {
				fmt.Printf("  ✗ GPU检测失败: %v\n", err)
			} else if gpuState.HasGPU {
				fmt.Printf("  ✓ 检测到 %d 个GPU设备:\n", len(gpuState.GPUs))
				for _, g := range gpuState.GPUs {
					fmt.Printf("    GPU%d: %s\n", g.Index, g.Name)
					fmt.Printf("      UUID: %s\n", g.UUID)
					fmt.Printf("      显存: %d MB (已用: %d MB, 空闲: %d MB)\n",
						g.TotalMemory, g.UsedMemory, g.FreeMemory)
					fmt.Printf("      温度: %d°C, 功耗: %d mW\n", g.Temperature, g.PowerUsage)
					if g.MIGEnabled {
						fmt.Printf("      MIG: 已启用, %d 个MIG实例\n", len(g.MIGDevices))
						for _, mig := range g.MIGDevices {
							fmt.Printf("        - %s (%d MB)\n", mig.Name, mig.MemoryMB)
						}
					}
				}

				gpuProcesses, err := gpu.QueryGPUProcesses()
				if err != nil {
					fmt.Printf("  ⚠ GPU进程查询失败: %v\n", err)
				} else if len(gpuProcesses) > 0 {
					fmt.Printf("\n  运行中的GPU进程: %d 个\n", len(gpuProcesses))
					for _, p := range gpuProcesses {
						fmt.Printf("    PID=%d %s (显存: %d MB)\n", p.PID, p.ProcessName, p.UsedMemory)
					}
				} else {
					fmt.Println("\n  无运行中的GPU进程")
				}
			} else {
				fmt.Println("  ✗ 未检测到GPU设备")
			}
		}

		if checkHost != "" {
			fmt.Printf("\n检查目标主机: %s:%d\n\n", checkHost, checkPort)

			res, err := resource.CheckTargetResources(checkHost, checkPort)
			if err != nil {
				return fmt.Errorf("检查目标主机失败: %w", err)
			}

			fmt.Println("目标主机资源:")
			fmt.Printf("  CPU核心数: %d\n", res.CPUCores)
			fmt.Printf("  总内存: %s\n", formatBytes(res.TotalMemory))
			fmt.Printf("  可用内存: %s\n", formatBytes(res.FreeMemory))
			fmt.Printf("  总磁盘: %s\n", formatBytes(res.TotalDisk))
			fmt.Printf("  可用磁盘: %s\n", formatBytes(res.FreeDisk))
			fmt.Printf("  系统负载: %.2f\n", res.LoadAvg)
			fmt.Printf("  LXC支持: %v\n", res.LXCSupport)
			fmt.Printf("  CRIU支持: %v\n", res.CRIUSupport)

			if res.HasGPU {
				fmt.Printf("  GPU: %d 个设备\n", len(res.GPUs))
				for _, g := range res.GPUs {
					fmt.Printf("    GPU%d: %s (%d MB 显存)\n", g.Index, g.Name, g.TotalMemory)
				}
			} else {
				fmt.Println("  GPU: 无")
			}

			if res.FreeMemory < 1024*1024*1024 {
				fmt.Println("\n  ⚠ 警告: 可用内存不足1GB")
			}
			if res.LoadAvg > float64(res.CPUCores)*2 {
				fmt.Println("  ⚠ 警告: 系统负载过高")
			}
		}

		fmt.Println("\n✓ 检查完成")
		return nil
	},
}

func formatBytes(bytes int64) string {
	const unit = 1024
	if bytes < unit {
		return fmt.Sprintf("%d B", bytes)
	}
	div, exp := int64(unit), 0
	for n := bytes / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %cB", float64(bytes)/float64(div), "KMGTPE"[exp])
}

func init() {
	checkCmd.Flags().StringVarP(&checkHost, "host", "H", "", "目标主机地址")
	checkCmd.Flags().IntVarP(&checkPort, "port", "p", 9999, "目标主机端口")
	checkCmd.Flags().BoolVar(&checkGPU, "gpu", false, "检测本地GPU资源")
}
