package cmd

import (
	"fmt"
	"time"

	"github.com/lxc-migrate/lxc-migrate/pkg/gpu"
	"github.com/lxc-migrate/lxc-migrate/pkg/lxc"
	"github.com/lxc-migrate/lxc-migrate/pkg/network"
	"github.com/lxc-migrate/lxc-migrate/pkg/predict"
	"github.com/lxc-migrate/lxc-migrate/pkg/progress"
	"github.com/lxc-migrate/lxc-migrate/pkg/resource"
	"github.com/lxc-migrate/lxc-migrate/pkg/transfer"
	"github.com/spf13/cobra"
)

var (
	targetHost    string
	targetPort    int
	containerName string
	checkpointDir string
	bwlimit       int
	preCopy       bool
	preCopyIter   int
	verbose       bool
	skipCheck     bool
	force         bool
	gpuMapping    string
	predictMode   bool
	autoMode      bool
	historyDir    string
)

var migrateCmd = &cobra.Command{
	Use:   "migrate [container]",
	Short: "迁移LXC容器到目标主机",
	Long: `将运行中的LXC容器从当前主机迁移到目标主机。
支持预拷贝模式、带宽限制、实时进度显示、GPU（NVIDIA CUDA）热迁移。

预测模式 (--predict):
  仅评估迁移可行性，不实际执行迁移。输出推荐迁移模式、
  预估总时间、服务中断时间和成功率。

自动模式 (--auto):
  根据脏页速率和带宽自动选择最佳迁移模式
  (预拷贝/后拷贝/直接) 并执行迁移。

GPU迁移说明:
  使用 --gpu 参数指定GPU设备映射。
  格式: 源GPU索引->目标GPU索引 (如: 0->1, 1->2)
  MIG格式: MIG-UUID-SRC->MIG-UUID-DST
  迁移前发送SIGUSR1通知应用释放GPU，恢复后发送SIGUSR2恢复GPU状态。`,
	Args: cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		containerName = args[0]

		fmt.Printf("=== LXC容器热迁移工具 ===\n")
		fmt.Printf("容器: %s\n", containerName)
		fmt.Printf("目标主机: %s:%d\n", targetHost, targetPort)
		fmt.Printf("预拷贝: %v\n", preCopy)
		fmt.Printf("自动模式: %v\n", autoMode)
		fmt.Printf("预测模式: %v\n", predictMode)
		if bwlimit > 0 {
			fmt.Printf("带宽限制: %d KB/s\n", bwlimit)
		}
		if gpuMapping != "" {
			fmt.Printf("GPU映射: %s\n", gpuMapping)
		}
		fmt.Println()

		mappings, err := gpu.ParseGPUMapping(gpuMapping)
		if err != nil {
			return fmt.Errorf("GPU映射格式错误: %w", err)
		}

		var sourceGPUState, targetGPUState *gpu.GPUState
		var gpuProcesses []gpu.GPUProcessInfo

		if gpuMapping != "" || !skipCheck {
			fmt.Println("[GPU] 检查GPU资源...")
			sourceGPUState, err = gpu.DetectGPU()
			if err != nil {
				if !force {
					return fmt.Errorf("检测源主机GPU失败: %w", err)
				}
				fmt.Println("  ⚠ GPU检测失败，因 --force 继续")
			} else if sourceGPUState.HasGPU {
				fmt.Printf("  源主机GPU: %d 个设备\n", len(sourceGPUState.GPUs))
				for _, g := range sourceGPUState.GPUs {
					fmt.Printf("    GPU%d: %s (%d MB 显存)\n", g.Index, g.Name, g.TotalMemory)
				}

				gpuProcesses, err = gpu.QueryGPUProcesses()
				if err != nil {
					fmt.Printf("  ⚠ 查询GPU进程失败: %v\n", err)
				} else if len(gpuProcesses) > 0 {
					fmt.Printf("  发现 %d 个GPU进程:\n", len(gpuProcesses))
					for _, p := range gpuProcesses {
						fmt.Printf("    PID=%d %s (%d MB 显存)\n", p.PID, p.ProcessName, p.UsedMemory)
					}
				}
			} else {
				fmt.Println("  源主机无GPU")
			}

			targetGPUState, err = gpu.CheckTargetGPUAvailable(targetHost, targetPort)
			if err != nil {
				if sourceGPUState != nil && sourceGPUState.HasGPU && !force {
					return fmt.Errorf("检查目标主机GPU失败: %w", err)
				}
				fmt.Println("  ⚠ 目标主机GPU检查失败")
			} else if targetGPUState.HasGPU {
				fmt.Printf("  目标主机GPU: %d 个设备\n", len(targetGPUState.GPUs))
				for _, g := range targetGPUState.GPUs {
					fmt.Printf("    GPU%d: %s (%d MB 显存)\n", g.Index, g.Name, g.TotalMemory)
				}
			} else {
				fmt.Println("  目标主机无GPU")
			}

			if sourceGPUState != nil && sourceGPUState.HasGPU {
				if err := resource.ValidateGPUResources(sourceGPUState, targetGPUState, mappings); err != nil {
					if !force {
						return fmt.Errorf("GPU资源验证失败: %w", err)
					}
					fmt.Println("  ⚠ GPU资源验证失败，因 --force 继续")
				} else {
					fmt.Println("  ✓ GPU资源兼容")
				}
			}
			fmt.Println()
		}

		predictor := predict.NewPredictor()
		collector := predict.NewFeedbackCollector(historyDir, predictor)

		var predResult *predict.PredictionResult

		if predictMode || autoMode {
			fmt.Println("[预测] 分析迁移可行性...")

			containerInfo, err := lxc.GetContainerInfo(containerName)
			if err != nil {
				return fmt.Errorf("获取容器信息失败: %w", err)
			}

			fmt.Println("  测量脏页速率...")
			dirtyRate, totalMem, err := predict.QuickDirtyRateEstimate(containerName)
			if err != nil {
				fmt.Printf("  ⚠ 脏页速率测量失败: %v\n", err)
				dirtyRate = float64(containerInfo.MemoryUsage) * 0.1
				totalMem = containerInfo.MemoryUsage
			}
			fmt.Printf("  脏页速率: %.1f MB/s\n", dirtyRate/(1024*1024))
			fmt.Printf("  总内存: %.1f MB\n", float64(totalMem)/(1024*1024))

			networkBW := float64(bwlimit) * 1024
			if networkBW == 0 {
				networkBW = 100 * 1024 * 1024
			}
			fmt.Printf("  网络带宽: %.1f MB/s\n", networkBW/(1024*1024))

			hasGPU := sourceGPUState != nil && sourceGPUState.HasGPU
			predResult = predictor.Predict(containerName, dirtyRate, totalMem, networkBW, hasGPU)

			fmt.Print(predResult.RecommendationText())

			if predictMode && !autoMode {
				fmt.Println("\n[预测模式] 评估完成，不执行迁移。")
				return nil
			}

			if autoMode && predResult != nil {
				switch predResult.RecommendedMode {
				case predict.ModePreCopy:
					preCopy = true
					if !cmd.Flags().Changed("pre-copy-iter") {
						preCopyIter = predResult.PreCopyIters
					}
					fmt.Printf("[自动模式] 选择预拷贝模式 (迭代: %d)\n", preCopyIter)
				case predict.ModePostCopy:
					preCopy = false
					fmt.Println("[自动模式] 选择后拷贝模式")
				default:
					preCopy = false
					fmt.Println("[自动模式] 选择直接迁移")
				}
				fmt.Println()
			}
		}

		migrationID := predict.GenerateMigrationID()
		migrationStart := time.Now()

		if !skipCheck {
			fmt.Println("[1/7] 检查源主机容器状态...")
			if err := lxc.CheckContainerExists(containerName); err != nil {
				collector.RecordFeedback(predict.MigrationRecord{
					ID:            migrationID,
					ContainerName: containerName,
					Timestamp:     time.Now(),
					Success:       false,
					Error:         err.Error(),
				})
				return fmt.Errorf("容器检查失败: %w", err)
			}
			if err := lxc.CheckContainerRunning(containerName); err != nil {
				collector.RecordFeedback(predict.MigrationRecord{
					ID:            migrationID,
					ContainerName: containerName,
					Timestamp:     time.Now(),
					Success:       false,
					Error:         err.Error(),
				})
				return fmt.Errorf("容器未运行: %w", err)
			}
			fmt.Println("  ✓ 容器状态正常")
		}

		if !skipCheck {
			fmt.Println("[2/7] 检查目标主机资源...")
			containerInfo, err := lxc.GetContainerInfo(containerName)
			if err != nil {
				return fmt.Errorf("获取容器信息失败: %w", err)
			}
			targetResource, err := resource.CheckTargetResources(targetHost, targetPort)
			if err != nil {
				if !force {
					return fmt.Errorf("目标主机资源检查失败: %w\n使用 --force 跳过检查", err)
				}
				fmt.Println("  ⚠ 资源检查失败，因 --force 继续")
			} else {
				if err := resource.ValidateResources(containerInfo, targetResource); err != nil {
					if !force {
						return fmt.Errorf("目标主机资源不足: %w\n使用 --force 跳过检查", err)
					}
					fmt.Println("  ⚠ 资源不足，因 --force 继续")
				} else {
					fmt.Println("  ✓ 目标主机资源充足")
				}
			}
		}

		fmt.Println("[3/7] 准备迁移环境...")
		if err := lxc.PrepareMigration(containerName); err != nil {
			return fmt.Errorf("准备迁移环境失败: %w", err)
		}
		fmt.Println("  ✓ 迁移环境准备完成")

		if sourceGPUState != nil && sourceGPUState.HasGPU && len(gpuProcesses) > 0 {
			fmt.Println("[3.5/7] 通知GPU应用释放资源...")
			if err := gpu.NotifyGPUAppsRelease(containerName, gpuProcesses); err != nil {
				fmt.Printf("  ⚠ GPU通知警告: %v\n", err)
			}
		}

		var totalBytes int64
		downtimeStart := time.Now()

		if preCopy {
			fmt.Println("[4/7] 执行预拷贝迁移...")
			totalBytes, err = executePreCopyMigration(mappings, sourceGPUState, gpuProcesses)
			if err != nil {
				collector.RecordFeedback(predict.MigrationRecord{
					ID:            migrationID,
					ContainerName: containerName,
					Timestamp:     time.Now(),
					Success:       false,
					Error:         err.Error(),
				})
				return fmt.Errorf("预拷贝迁移失败: %w", err)
			}
		} else {
			fmt.Println("[4/7] 执行直接迁移...")
			totalBytes, err = executeDirectMigration(mappings, sourceGPUState, gpuProcesses)
			if err != nil {
				collector.RecordFeedback(predict.MigrationRecord{
					ID:            migrationID,
					ContainerName: containerName,
					Timestamp:     time.Now(),
					Success:       false,
					Error:         err.Error(),
				})
				return fmt.Errorf("迁移失败: %w", err)
			}
		}

		fmt.Println("[5/7] 恢复容器在目标主机...")
		if err := waitForRestore(targetHost, targetPort); err != nil {
			collector.RecordFeedback(predict.MigrationRecord{
				ID:            migrationID,
				ContainerName: containerName,
				Timestamp:     time.Now(),
				Success:       false,
				Error:         err.Error(),
			})
			return fmt.Errorf("恢复容器失败: %w", err)
		}
		fmt.Println("  ✓ 容器恢复成功")

		actualDowntime := time.Since(downtimeStart).Seconds() * 1000

		if sourceGPUState != nil && sourceGPUState.HasGPU {
			fmt.Println("[5.5/7] 恢复GPU状态...")
			if err := gpu.TestGPUAvailability(containerName); err != nil {
				if !force {
					return fmt.Errorf("GPU可用性测试失败: %w", err)
				}
				fmt.Printf("  ⚠ GPU可用性测试警告: %v\n", err)
			}
			if err := gpu.NotifyGPUAppsRestore(containerName, gpuProcesses); err != nil {
				fmt.Printf("  ⚠ GPU恢复通知警告: %v\n", err)
			}
		}

		fmt.Println("[6/7] 更新网络配置...")
		if err := network.UpdateNetworkConfig(targetHost, targetPort, containerName); err != nil {
			fmt.Printf("  ⚠ 网络配置更新警告: %v\n", err)
		} else {
			fmt.Println("  ✓ 网络配置已更新")
		}

		fmt.Println("[7/7] 清理源主机容器...")
		if err := lxc.StopContainer(containerName); err != nil {
			fmt.Printf("  ⚠ 停止源容器警告: %v (可手动停止)\n", err)
		} else {
			fmt.Println("  ✓ 源容器已停止")
		}

		totalTime := time.Since(migrationStart).Seconds()

		dirtyRate := 0.0
		totalMem := int64(0)
		if predResult != nil {
			dirtyRate = predResult.DirtyRate
			totalMem = predResult.TotalMemory
		}

		mode := predict.ModeDirect
		if preCopy {
			mode = predict.ModePreCopy
		}

		networkBW := float64(bwlimit) * 1024
		if networkBW == 0 {
			networkBW = 100 * 1024 * 1024
		}

		collector.RecordFeedback(predict.MigrationRecord{
			ID:              migrationID,
			ContainerName:   containerName,
			Timestamp:       time.Now(),
			SourceHost:      "localhost",
			TargetHost:      targetHost,
			Mode:            mode,
			PreCopyIters:    preCopyIter,
			DirtyRate:       dirtyRate,
			TotalMemory:     totalMem,
			NetworkBW:       networkBW,
			HasGPU:          sourceGPUState != nil && sourceGPUState.HasGPU,
			ActualTotalTime: totalTime,
			ActualDowntime:  actualDowntime,
			Success:         true,
			PredictedTime:   0,
			PredictedDown:   0,
			DataTransferred: totalBytes,
		})

		fmt.Println()
		fmt.Println("=== 迁移完成 ===")
		fmt.Printf("迁移ID: %s\n", migrationID)
		fmt.Printf("传输数据量: %s\n", progress.FormatBytes(totalBytes))
		fmt.Printf("目标主机: %s:%d\n", targetHost, targetPort)
		fmt.Printf("总时间: %.1f 秒\n", totalTime)
		fmt.Printf("服务中断: %.0f 毫秒\n", actualDowntime)
		if sourceGPUState != nil && sourceGPUState.HasGPU {
			fmt.Printf("GPU映射: %s\n", gpu.FormatGPUMapping(mappings))
		}

		if predResult != nil {
			fmt.Println()
			fmt.Println("--- 预测对比 ---")
			fmt.Printf("推荐模式: %s\n", predResult.RecommendedMode)
			fmt.Printf("预估总时间: %.1f 秒 (实际: %.1f 秒)\n", predResult.EstimatedTotalTime, totalTime)
			fmt.Printf("预估中断: %.0f 毫秒 (实际: %.0f 毫秒)\n", predResult.EstimatedDowntime, actualDowntime)
			fmt.Printf("预估成功率: %.1f%%\n", predResult.SuccessProbability*100)
		}

		return nil
	},
}

func executeDirectMigration(mappings []gpu.GPUDeviceMapping, sourceGPUState *gpu.GPUState, gpuProcesses []gpu.GPUProcessInfo) (int64, error) {
	checkpointPath := fmt.Sprintf("%s/%s", checkpointDir, containerName)

	fmt.Println("  创建容器检查点...")
	if err := lxc.CreateCheckpoint(containerName, checkpointPath, true); err != nil {
		return 0, fmt.Errorf("创建检查点失败: %w", err)
	}
	defer lxc.CleanupCheckpoint(checkpointPath)

	if sourceGPUState != nil && sourceGPUState.HasGPU && len(gpuProcesses) > 0 {
		fmt.Println("  保存GPU状态...")
		if _, err := gpu.SaveGPUState(containerName, checkpointPath, gpuProcesses); err != nil {
			fmt.Printf("  ⚠ 保存GPU状态警告: %v\n", err)
		}
	}

	fmt.Println("  连接目标主机...")
	conn, err := transfer.ConnectTarget(targetHost, targetPort)
	if err != nil {
		return 0, fmt.Errorf("连接目标主机失败: %w", err)
	}
	defer conn.Close()

	fmt.Println("  传输容器数据...")
	pb := progress.NewProgressBar("传输中")
	pb.Start()

	opts := transfer.TransferOptions{
		Bwlimit:    bwlimit,
		ProgressCb: pb.Update,
	}
	totalBytes, err := transfer.SendDirectory(conn, checkpointPath, opts)
	if err != nil {
		pb.Finish()
		return 0, fmt.Errorf("传输失败: %w", err)
	}
	pb.Finish()

	if err := transfer.SendRestoreCommand(conn, containerName); err != nil {
		return 0, fmt.Errorf("发送恢复命令失败: %w", err)
	}

	return totalBytes, nil
}

func executePreCopyMigration(mappings []gpu.GPUDeviceMapping, sourceGPUState *gpu.GPUState, gpuProcesses []gpu.GPUProcessInfo) (int64, error) {
	checkpointPath := fmt.Sprintf("%s/%s-precopy", checkpointDir, containerName)
	var totalBytes int64

	conn, err := transfer.ConnectTarget(targetHost, targetPort)
	if err != nil {
		return 0, fmt.Errorf("连接目标主机失败: %w", err)
	}
	defer conn.Close()

	for i := 1; i <= preCopyIter; i++ {
		isFinal := i == preCopyIter
		fmt.Printf("  预拷贝迭代 %d/%d (最后迭代: %v)...\n", i, preCopyIter, isFinal)

		if err := lxc.CreateCheckpoint(containerName, checkpointPath, !isFinal); err != nil {
			return totalBytes, fmt.Errorf("创建检查点失败(迭代%d): %w", i, err)
		}

		if isFinal && sourceGPUState != nil && sourceGPUState.HasGPU && len(gpuProcesses) > 0 {
			fmt.Println("  保存最终GPU状态...")
			if _, err := gpu.SaveGPUState(containerName, checkpointPath, gpuProcesses); err != nil {
				fmt.Printf("  ⚠ 保存GPU状态警告: %v\n", err)
			}
		}

		pb := progress.NewProgressBar(fmt.Sprintf("传输迭代%d", i))
		pb.Start()

		opts := transfer.TransferOptions{
			Bwlimit:    bwlimit,
			ProgressCb: pb.Update,
		}
		bytes, err := transfer.SendDirectory(conn, checkpointPath, opts)
		if err != nil {
			pb.Finish()
			return totalBytes, fmt.Errorf("传输失败(迭代%d): %w", i, err)
		}
		pb.Finish()
		totalBytes += bytes

		if !isFinal {
			fmt.Println("  等待脏页产生...")
			time.Sleep(2 * time.Second)
		}

		lxc.CleanupCheckpoint(checkpointPath)
	}

	if err := transfer.SendRestoreCommand(conn, containerName); err != nil {
		return totalBytes, fmt.Errorf("发送恢复命令失败: %w", err)
	}

	return totalBytes, nil
}

func waitForRestore(host string, port int) error {
	deadline := time.Now().Add(30 * time.Second)
	for time.Now().Before(deadline) {
		healthy, err := resource.CheckContainerHealth(host, port, containerName)
		if err == nil && healthy {
			return nil
		}
		time.Sleep(500 * time.Millisecond)
	}
	return fmt.Errorf("等待容器恢复超时")
}

func init() {
	migrateCmd.Flags().StringVarP(&targetHost, "host", "H", "127.0.0.1", "目标主机地址")
	migrateCmd.Flags().IntVarP(&targetPort, "port", "p", 9999, "目标主机端口")
	migrateCmd.Flags().StringVar(&checkpointDir, "checkpoint-dir", "/tmp/lxc-migrate", "检查点临时目录")
	migrateCmd.Flags().IntVar(&bwlimit, "bwlimit", 0, "带宽限制(KB/s)，0表示无限制")
	migrateCmd.Flags().BoolVar(&preCopy, "pre-copy", false, "启用预拷贝模式")
	migrateCmd.Flags().IntVar(&preCopyIter, "pre-copy-iter", 3, "预拷贝迭代次数")
	migrateCmd.Flags().BoolVarP(&verbose, "verbose", "v", false, "详细输出")
	migrateCmd.Flags().BoolVar(&skipCheck, "skip-check", false, "跳过资源检查")
	migrateCmd.Flags().BoolVar(&force, "force", false, "强制迁移(忽略资源不足)")
	migrateCmd.Flags().StringVar(&gpuMapping, "gpu", "", "GPU设备映射 (格式: 源->目标，如 0->1,1->2 或 MIG-UUID-SRC->MIG-UUID-DST)")
	migrateCmd.Flags().BoolVar(&predictMode, "predict", false, "预测模式: 仅评估不执行迁移")
	migrateCmd.Flags().BoolVar(&autoMode, "auto", false, "自动模式: 根据脏页速率自动选择迁移模式")
	migrateCmd.Flags().StringVar(&historyDir, "history-dir", "/tmp/lxc-migrate/history", "迁移历史数据目录")
}
