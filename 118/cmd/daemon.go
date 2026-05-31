package cmd

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/lxc-migrate/lxc-migrate/pkg/gpu"
	"github.com/lxc-migrate/lxc-migrate/pkg/lxc"
	"github.com/lxc-migrate/lxc-migrate/pkg/predict"
	"github.com/lxc-migrate/lxc-migrate/pkg/transfer"
	"github.com/spf13/cobra"
)

var (
	daemonPort      int
	daemonDir       string
	daemonVerbose   bool
	daemonHistoryDir string
)

var daemonCmd = &cobra.Command{
	Use:   "daemon",
	Short: "启动目标主机守护进程",
	Long: `在目标主机上启动守护进程，用于接收容器迁移数据并恢复容器。
支持GPU（NVIDIA CUDA）容器的迁移恢复和预测模型反馈收集。
此命令应在目标主机上运行。`,
	RunE: func(cmd *cobra.Command, args []string) error {
		fmt.Printf("=== LXC迁移守护进程 ===\n")
		fmt.Printf("监听端口: %d\n", daemonPort)
		fmt.Printf("临时目录: %s\n", daemonDir)
		fmt.Printf("历史数据目录: %s\n", daemonHistoryDir)
		fmt.Println()

		if err := os.MkdirAll(daemonDir, 0755); err != nil {
			return fmt.Errorf("创建临时目录失败: %w", err)
		}

		if err := os.MkdirAll(daemonHistoryDir, 0755); err != nil {
			return fmt.Errorf("创建历史目录失败: %w", err)
		}

		predictor := predict.NewPredictor()
		collector := predict.NewFeedbackCollector(daemonHistoryDir, predictor)

		gpuState, err := gpu.DetectGPU()
		if err != nil {
			fmt.Printf("GPU检测: 失败 (%v)\n", err)
		} else if gpuState.HasGPU {
			fmt.Printf("GPU检测: 发现 %d 个GPU设备\n", len(gpuState.GPUs))
			for _, g := range gpuState.GPUs {
				fmt.Printf("  GPU%d: %s (%d MB 显存)\n", g.Index, g.Name, g.TotalMemory)
			}
		} else {
			fmt.Println("GPU检测: 未发现GPU")
		}

		total, success, failed, avgTime, avgDowntime := collector.GetStore().Stats()
		fmt.Printf("历史统计: 总计 %d, 成功 %d, 失败 %d, 平均时间 %.1fs, 平均中断 %.0fms\n",
			total, success, failed, avgTime, avgDowntime)

		listener, err := net.Listen("tcp", fmt.Sprintf(":%d", daemonPort))
		if err != nil {
			return fmt.Errorf("监听失败: %w", err)
		}
		defer listener.Close()

		fmt.Printf("守护进程已启动，等待连接...\n")

		sigCh := make(chan os.Signal, 1)
		signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

		go func() {
			<-sigCh
			fmt.Println("\n收到终止信号，正在关闭...")
			collector.GetStore().Save()
			listener.Close()
			os.Exit(0)
		}()

		for {
			conn, err := listener.Accept()
			if err != nil {
				log.Printf("接受连接失败: %v", err)
				continue
			}
			fmt.Printf("新连接: %s\n", conn.RemoteAddr())
			go handleConnection(conn, collector)
		}
	},
}

func handleConnection(conn net.Conn, collector *predict.FeedbackCollector) {
	defer conn.Close()

	reader := bufio.NewReader(conn)

	peekBuf, err := reader.Peek(4)
	if err != nil {
		log.Printf("Peek失败: %v", err)
		return
	}

	firstByte := peekBuf[0]
	if firstByte == '{' {
		handleCheckRequest(reader, conn, collector)
		return
	}

	handleMigration(reader, conn, collector)
}

func handleCheckRequest(reader *bufio.Reader, conn net.Conn, collector *predict.FeedbackCollector) {
	lenBuf := make([]byte, 4)
	if _, err := reader.Read(lenBuf); err != nil {
		log.Printf("读取请求长度失败: %v", err)
		return
	}

	reqLen := binaryBigEndianUint32(lenBuf)
	reqBuf := make([]byte, reqLen)
	if _, err := reader.Read(reqBuf); err != nil {
		log.Printf("读取请求数据失败: %v", err)
		return
	}

	var request map[string]interface{}
	if err := json.Unmarshal(reqBuf, &request); err != nil {
		log.Printf("解析请求失败: %v", err)
		return
	}

	action, _ := request["action"].(string)

	switch action {
	case "check_gpu":
		handleGPUCheck(conn)
	case "check_resources":
		handleResourceCheck(conn)
	case "check_health":
		handleHealthCheck(conn, request)
	case "check_history":
		handleHistoryCheck(conn, collector)
	case "check_stats":
		handleStatsCheck(conn, collector)
	default:
		log.Printf("未知操作: %s", action)
	}
}

func handleGPUCheck(conn net.Conn) {
	gpuState, err := gpu.DetectGPU()
	if err != nil {
		log.Printf("GPU检测失败: %v", err)
		gpuState = &gpu.GPUState{HasGPU: false}
	}

	resp, _ := json.Marshal(gpuState)

	lenBuf := make([]byte, 4)
	putUint32BigEndian(lenBuf, uint32(len(resp)))
	conn.Write(lenBuf)
	conn.Write(resp)
}

func handleResourceCheck(conn net.Conn) {
	resources, err := resourceCheckLocal()
	if err != nil {
		log.Printf("资源检查失败: %v", err)
		resources = &hostResourcesMinimal{
			LXCSupport:  true,
			CRIUSupport: true,
		}
	}

	resp, _ := json.Marshal(resources)

	lenBuf := make([]byte, 4)
	putUint32BigEndian(lenBuf, uint32(len(resp)))
	conn.Write(lenBuf)
	conn.Write(resp)
}

func handleHealthCheck(conn net.Conn, request map[string]interface{}) {
	container, _ := request["container"].(string)

	healthy := false
	if container != "" {
		if err := lxc.CheckContainerRunning(container); err == nil {
			healthy = true
		}
	}

	resp := map[string]interface{}{
		"healthy": healthy,
	}
	data, _ := json.Marshal(resp)

	lenBuf := make([]byte, 4)
	putUint32BigEndian(lenBuf, uint32(len(data)))
	conn.Write(lenBuf)
	conn.Write(data)
}

func handleHistoryCheck(conn net.Conn, collector *predict.FeedbackCollector) {
	records := collector.GetStore().GetAllRecords()
	resp, _ := json.Marshal(records)

	lenBuf := make([]byte, 4)
	putUint32BigEndian(lenBuf, uint32(len(resp)))
	conn.Write(lenBuf)
	conn.Write(resp)
}

func handleStatsCheck(conn net.Conn, collector *predict.FeedbackCollector) {
	total, success, failed, avgTime, avgDowntime := collector.GetStore().Stats()
	resp := map[string]interface{}{
		"total":         total,
		"success":       success,
		"failed":        failed,
		"avg_time_sec":  avgTime,
		"avg_downtime_ms": avgDowntime,
	}
	data, _ := json.Marshal(resp)

	lenBuf := make([]byte, 4)
	putUint32BigEndian(lenBuf, uint32(len(data)))
	conn.Write(lenBuf)
	conn.Write(data)
}

func handleMigration(reader *bufio.Reader, conn net.Conn, collector *predict.FeedbackCollector) {
	migrationStart := time.Now()
	fmt.Println("开始接收容器数据...")

	containerName, err := transfer.ReceiveHeader(reader)
	if err != nil {
		log.Printf("接收头部失败: %v", err)
		collector.RecordFeedback(predict.MigrationRecord{
			ID:            predict.GenerateMigrationID(),
			ContainerName: "unknown",
			Timestamp:     time.Now(),
			Success:       false,
			Error:         fmt.Sprintf("接收头部失败: %v", err),
		})
		return
	}
	fmt.Printf("容器名称: %s\n", containerName)

	containerDir := fmt.Sprintf("%s/%s", daemonDir, containerName)
	if err := os.MkdirAll(containerDir, 0755); err != nil {
		log.Printf("创建容器目录失败: %v", err)
		collector.RecordFeedback(predict.MigrationRecord{
			ID:            predict.GenerateMigrationID(),
			ContainerName: containerName,
			Timestamp:     time.Now(),
			Success:       false,
			Error:         fmt.Sprintf("创建目录失败: %v", err),
		})
		return
	}

	if err := transfer.ReceiveDirectory(reader, containerDir); err != nil {
		log.Printf("接收数据失败: %v", err)
		collector.RecordFeedback(predict.MigrationRecord{
			ID:            predict.GenerateMigrationID(),
			ContainerName: containerName,
			Timestamp:     time.Now(),
			Success:       false,
			Error:         fmt.Sprintf("接收数据失败: %v", err),
		})
		return
	}

	fmt.Println("数据接收完成，开始恢复容器...")

	if err := lxc.RestoreContainer(containerName, containerDir); err != nil {
		log.Printf("恢复容器失败: %v", err)
		collector.RecordFeedback(predict.MigrationRecord{
			ID:            predict.GenerateMigrationID(),
			ContainerName: containerName,
			Timestamp:     time.Now(),
			Success:       false,
			Error:         fmt.Sprintf("恢复失败: %v", err),
		})
		return
	}

	restoreTime := time.Since(migrationStart).Seconds()
	fmt.Printf("容器 %s 恢复成功! (耗时: %.1f秒)\n", containerName, restoreTime)

	gpuStateFile := fmt.Sprintf("%s/gpu_state.json", containerDir)
	hasGPU := false
	if _, err := os.Stat(gpuStateFile); err == nil {
		hasGPU = true
		fmt.Println("检测到GPU状态文件，执行GPU恢复...")

		mappings, _ := gpu.ParseGPUMapping("")

		if _, err := gpu.RestoreGPUState(containerName, containerDir, mappings); err != nil {
			log.Printf("GPU状态恢复失败: %v", err)
		} else {
			fmt.Println("GPU状态恢复成功!")

			if err := gpu.TestGPUAvailability(containerName); err != nil {
				log.Printf("GPU可用性测试失败: %v", err)
			}
		}
	}

	collector.RecordFeedback(predict.MigrationRecord{
		ID:              predict.GenerateMigrationID(),
		ContainerName:   containerName,
		Timestamp:       time.Now(),
		TargetHost:      "localhost",
		Mode:            predict.ModeDirect,
		HasGPU:          hasGPU,
		ActualTotalTime: restoreTime,
		ActualDowntime:  restoreTime * 1000,
		Success:         true,
	})

	if err := lxc.CleanupCheckpoint(containerDir); err != nil {
		log.Printf("清理临时文件失败: %v", err)
	}
}

func init() {
	daemonCmd.Flags().IntVarP(&daemonPort, "port", "p", 9999, "监听端口")
	daemonCmd.Flags().StringVar(&daemonDir, "data-dir", "/tmp/lxc-migrate", "数据接收目录")
	daemonCmd.Flags().BoolVarP(&daemonVerbose, "verbose", "v", false, "详细输出")
	daemonCmd.Flags().StringVar(&daemonHistoryDir, "history-dir", "/tmp/lxc-migrate/history", "迁移历史数据目录")
}

func binaryBigEndianUint32(buf []byte) uint32 {
	return uint32(buf[0])<<24 | uint32(buf[1])<<16 | uint32(buf[2])<<8 | uint32(buf[3])
}

func putUint32BigEndian(buf []byte, v uint32) {
	buf[0] = byte(v >> 24)
	buf[1] = byte(v >> 16)
	buf[2] = byte(v >> 8)
	buf[3] = byte(v)
}

type hostResourcesMinimal struct {
	CPUCores    int     `json:"cpu_cores"`
	TotalMemory int64   `json:"total_memory"`
	FreeMemory  int64   `json:"free_memory"`
	TotalDisk   int64   `json:"total_disk"`
	FreeDisk    int64   `json:"free_disk"`
	LoadAvg     float64 `json:"load_avg"`
	LXCSupport  bool    `json:"lxc_support"`
	CRIUSupport bool    `json:"criu_support"`
	HasGPU      bool    `json:"has_gpu"`
}

func resourceCheckLocal() (*hostResourcesMinimal, error) {
	resources := &hostResourcesMinimal{
		LXCSupport:  true,
		CRIUSupport: true,
	}

	gpuState, err := gpu.DetectGPU()
	if err == nil && gpuState.HasGPU {
		resources.HasGPU = true
	}

	return resources, nil
}
