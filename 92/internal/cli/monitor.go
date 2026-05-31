package cli

import (
	"fmt"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/fatih/color"
	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/container"
	"github.com/io-qos/io-qos/pkg/latency"
	"github.com/io-qos/io-qos/pkg/logger"
	"github.com/io-qos/io-qos/pkg/monitor"
	"github.com/io-qos/io-qos/pkg/throttle"
	"github.com/io-qos/io-qos/pkg/web"
	"github.com/io-qos/io-qos/pkg/weight"
	"github.com/spf13/cobra"
)

var monitorCmd = &cobra.Command{
	Use:   "monitor [container IDs...]",
	Short: "实时监控容器IO使用情况",
	Long: `实时显示一个或多个容器的IO使用情况，包括：
- 当前读写带宽 (B/s, KB/s, MB/s, GB/s)
- 当前读写IOPS
- 等待队列长度
- 等待时间
- 权重状态（饿死检测和自动调整）
- IO延迟监控和主动限速`,
	Example: `  # 监控单个容器
  io-qos monitor container1

  # 监控多个容器，刷新间隔2秒
  io-qos monitor container1 container2 --interval 2s

  # 监控所有容器
  io-qos monitor --all

  # 启用饿死检测和自动权重调整
  sudo io-qos monitor --all --auto-adjust --starvation-timeout 30s

  # 设置饿死检测窗口为60秒
  sudo io-qos monitor --all --auto-adjust --starvation-timeout 60s

  # 启用IO延迟预测和主动限速
  sudo io-qos monitor --all --auto-throttle --latency-threshold 50ms

  # 启用Web控制面板
  sudo io-qos monitor --all --auto-throttle --web --web-port 8080`,
	RunE: runMonitor,
}

func init() {
	rootCmd.AddCommand(monitorCmd)

	monitorCmd.Flags().Bool("all", false, "监控所有可发现的容器")
	monitorCmd.Flags().Duration("interval", 1*time.Second, "监控刷新间隔")
	monitorCmd.Flags().Bool("no-color", false, "禁用彩色输出")
	monitorCmd.Flags().Bool("auto-adjust", false, "启用饿死检测和权重自动调整")
	monitorCmd.Flags().Duration("starvation-timeout", 30*time.Second, "饿死检测超时时间")
	monitorCmd.Flags().Duration("cooldown-time", 5*time.Second, "权重恢复后的冷却时间")
	monitorCmd.Flags().Duration("min-boost-time", 10*time.Second, "最小权重提升持续时间")
	monitorCmd.Flags().Bool("auto-throttle", false, "启用IO延迟监控和主动限速")
	monitorCmd.Flags().Float64("latency-threshold", 50, "IO延迟阈值(ms)")
	monitorCmd.Flags().Duration("latency-duration", 10*time.Second, "延迟持续时间")
	monitorCmd.Flags().Float64("reduction-ratio", 0.5, "限速缩减比例")
	monitorCmd.Flags().Int64("min-bps", 1048576, "保底带宽(B/s)")
	monitorCmd.Flags().Int64("min-iops", 10, "保底IOPS")
	monitorCmd.Flags().Bool("web", false, "启用Web控制面板")
	monitorCmd.Flags().Int("web-port", 8080, "Web控制面板端口")
	monitorCmd.Flags().String("log-level", "info", "日志级别 (debug, info, warn, error)")
	monitorCmd.Flags().String("log-dir", "/var/log/io-qos", "日志目录路径")
}

func runMonitor(cmd *cobra.Command, args []string) error {
	cgroupRoot, _ := cmd.Flags().GetString("cgroup-root")
	interval, _ := cmd.Flags().GetDuration("interval")
	noColor, _ := cmd.Flags().GetBool("no-color")
	all, _ := cmd.Flags().GetBool("all")
	autoAdjust, _ := cmd.Flags().GetBool("auto-adjust")
	starvationTimeout, _ := cmd.Flags().GetDuration("starvation-timeout")
	cooldownTime, _ := cmd.Flags().GetDuration("cooldown-time")
	minBoostTime, _ := cmd.Flags().GetDuration("min-boost-time")
	autoThrottle, _ := cmd.Flags().GetBool("auto-throttle")
	latencyThreshold, _ := cmd.Flags().GetFloat64("latency-threshold")
	latencyDuration, _ := cmd.Flags().GetDuration("latency-duration")
	reductionRatio, _ := cmd.Flags().GetFloat64("reduction-ratio")
	minBPS, _ := cmd.Flags().GetInt64("min-bps")
	minIOPS, _ := cmd.Flags().GetInt64("min-iops")
	enableWeb, _ := cmd.Flags().GetBool("web")
	webPort, _ := cmd.Flags().GetInt("web-port")
	logLevel, _ := cmd.Flags().GetString("log-level")
	logDir, _ := cmd.Flags().GetString("log-dir")

	if noColor {
		color.NoColor = true
	}

	log, err := logger.Init(logDir, logger.ParseLevel(logLevel))
	if err != nil {
		fmt.Fprintf(os.Stderr, "警告: 日志初始化失败: %v\n", err)
		fmt.Fprintf(os.Stderr, "日志将只输出到标准输出\n")
	}
	if log != nil {
		defer log.Close()
	}

	discoverer := container.NewDiscovererWithRoot(cgroupRoot)

	var containers []container.ContainerInfo

	if all || len(args) == 0 {
		containers, err = discoverer.DiscoverAll()
	} else {
		containers, err = discoverer.Discover(args)
	}

	if err != nil {
		return err
	}

	if len(containers) == 0 {
		return fmt.Errorf("no containers found to monitor")
	}

	fmt.Printf("发现 %d 个容器，开始监控 (刷新间隔: %v)\n", len(containers), interval)
	if autoAdjust {
		fmt.Printf("饿死检测已启用 (超时: %v)\n", starvationTimeout)
	}
	if autoThrottle {
		fmt.Printf("主动限速已启用 (阈值: %.0fms/%v, 缩减比例: %.0f%%)\n",
			latencyThreshold, latencyDuration, reductionRatio*100)
	}
	if enableWeb {
		fmt.Printf("Web控制面板已启用: http://localhost:%d\n", webPort)
	}
	fmt.Println("按 Ctrl+C 停止监控")
	fmt.Println()

	mon := monitor.NewMonitorWithRoot(interval, cgroupRoot)

	var targets []monitor.ContainerTarget
	var weightMgr *weight.WeightManager
	var throttleMgr *throttle.ThrottleManager
	var latencyMon *latency.LatencyMonitor
	var webServer *web.WebServer

	if autoAdjust {
		weightMgr = weight.NewWeightManager(starvationTimeout, cgroupRoot)
		weightMgr.SetCooldownTime(cooldownTime)
		weightMgr.SetMinBoostTime(minBoostTime)

		for _, c := range containers {
			priority := "medium"
			weightMgr.RegisterContainer(c.ID, c.Name, c.CgroupPath, priority)
		}

		logger.Info("[Monitor] Weight manager initialized with starvation timeout: %v", starvationTimeout)
	}

	if autoThrottle {
		latencyMon = latency.NewLatencyMonitor(time.Second)
		latencyMon.Start()

		throttleMgr = throttle.NewThrottleManager(cgroupRoot, latencyThreshold, latencyDuration)
		throttleMgr.SetReductionRatio(reductionRatio)
		throttleMgr.SetMinBandwidth(minBPS, minIOPS)

		for _, c := range containers {
			throttleMgr.RegisterContainer(c, "medium")
		}

		logger.Info("[Monitor] Throttle manager initialized with latency threshold: %.0fms/%v",
			latencyThreshold, latencyDuration)
	}

	if enableWeb {
		if latencyMon == nil {
			latencyMon = latency.NewLatencyMonitor(time.Second)
			latencyMon.Start()
		}
		if throttleMgr == nil {
			throttleMgr = throttle.NewThrottleManager(cgroupRoot, latencyThreshold, latencyDuration)
		}

		webServer = web.NewWebServer(webPort, latencyMon, throttleMgr)

		go func() {
			if err := webServer.Start(); err != nil {
				fmt.Fprintf(os.Stderr, "Web服务器错误: %v\n", err)
			}
		}()
	}

	for _, c := range containers {
		targets = append(targets, monitor.ContainerTarget{
			ContainerID:   c.ID,
			ContainerName: c.Name,
			CgroupPath:    c.CgroupPath,
		})

		fmt.Printf("  - %s (%s) -> %s\n", c.Name, c.ID[:12], c.CgroupPath)
	}
	fmt.Println()

	collector := monitor.NewStatsCollector(mon, targets)

	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)

	collector.Start()
	defer collector.Stop()

	if latencyMon != nil {
		defer latencyMon.Stop()
	}
	if webServer != nil {
		defer webServer.Stop()
	}

	clearScreen()

	for {
		select {
		case <-sigChan:
			fmt.Println("\n停止监控...")
			if autoAdjust && weightMgr != nil {
				logger.Info("[Monitor] Monitoring stopped. Final weight states: %s", weightMgr.GetSummary())
			}
			if autoThrottle && throttleMgr != nil {
				logger.Info("[Monitor] Monitoring stopped. Final throttle states: %s", throttleMgr.GetSummary())
			}
			return nil
		case err := <-collector.ErrorChan():
			fmt.Fprintf(os.Stderr, "监控错误: %v\n", err)
		case stats := <-collector.StatsChan():
			clearScreen()
			printMonitorHeader(interval, autoAdjust, autoThrottle, starvationTimeout, latencyThreshold)
			for _, s := range stats {
				if autoAdjust && weightMgr != nil {
					weightMgr.CheckAndAdjust(s.ContainerID, s.WaitTime, s.QueueLength)
				}
				printStatsRow(s, weightMgr, throttleMgr)
			}
			printMonitorFooter(len(stats), weightMgr, throttleMgr)

			if autoThrottle && throttleMgr != nil && latencyMon != nil {
				devices := latencyMon.GetAllDevices()
				var avgLatency float64
				for _, dev := range devices {
					lat := latencyMon.GetCurrentLatency(dev)
					avgLatency += lat.AvgLatency
				}
				if len(devices) > 0 {
					avgLatency /= float64(len(devices))
				}
				throttleMgr.CheckAndThrottle(avgLatency)
			}
		}
	}
}

func clearScreen() {
	fmt.Print("\033[2J\033[H")
}

func printMonitorHeader(interval time.Duration, autoAdjust, autoThrottle bool, starvationTimeout time.Duration, latencyThreshold float64) {
	cyan := color.New(color.FgCyan, color.Bold).SprintFunc()
	white := color.New(color.FgWhite, color.Bold).SprintFunc()
	yellow := color.New(color.FgYellow).SprintFunc()
	magenta := color.New(color.FgMagenta).SprintFunc()

	fmt.Printf("%s (刷新间隔: %v)\n", cyan("容器IO实时监控"), interval)
	if autoAdjust {
		fmt.Printf("%s %s %s\n", yellow("[饿死检测已启用]"), "超时:", yellow(starvationTimeout.String()))
	}
	if autoThrottle {
		fmt.Printf("%s %s %.0fms\n", magenta("[主动限速已启用]"), "阈值:", magenta(latencyThreshold))
	}

	header := fmt.Sprintf(
		"%-22s %12s %12s %10s %10s %8s %8s",
		white("容器名称"),
		white("读带宽"),
		white("写带宽"),
		white("读IOPS"),
		white("写IOPS"),
		white("队列"),
		white("等待"),
	)

	if autoAdjust {
		header += fmt.Sprintf(" %12s %8s", white("权重状态"), white("权重"))
	}
	if autoThrottle {
		header += fmt.Sprintf(" %10s", white("限速状态"))
	}

	fmt.Println(header)
	fmt.Println(strings.Repeat("-", 140))
}

func printStatsRow(s types.IOStats, weightMgr *weight.WeightManager, throttleMgr *throttle.ThrottleManager) {
	yellow := color.New(color.FgYellow).SprintFunc()
	green := color.New(color.FgGreen).SprintFunc()
	red := color.New(color.FgRed).SprintFunc()
	cyan := color.New(color.FgCyan).SprintFunc()
	magenta := color.New(color.FgMagenta).SprintFunc()

	readBW := monitor.FormatBandwidth(s.ReadBPS)
	writeBW := monitor.FormatBandwidth(s.WriteBPS)
	readIOPS := monitor.FormatIOPS(s.ReadIOPS)
	writeIOPS := monitor.FormatIOPS(s.WriteIOPS)

	queueColor := yellow
	if s.QueueLength > 100 {
		queueColor = red
	}

	name := s.ContainerName
	if len(name) > 20 {
		name = name[:17] + "..."
	}

	line := fmt.Sprintf(
		"%-22s %12s %12s %10s %10s %8s %8s",
		green(name),
		readBW,
		writeBW,
		readIOPS,
		writeIOPS,
		queueColor(s.QueueLength),
		fmt.Sprintf("%dms", s.WaitTime/1000),
	)

	if weightMgr != nil {
		state := weightMgr.GetState(s.ContainerID)
		if state != nil {
			stateStr := formatWeightState(state.State)
			weightStr := fmt.Sprintf("%d", state.CurrentWeight)

			if state.State == weight.StateElevated {
				stateStr = cyan(stateStr)
				weightStr = yellow(weightStr)
			}

			line += fmt.Sprintf(" %12s %8s", stateStr, weightStr)
		}
	}

	if throttleMgr != nil {
		state := throttleMgr.GetState(s.ContainerID)
		if state != nil {
			stateStr := formatThrottleState(state.State)
			if state.State == throttle.StateThrottled {
				stateStr = magenta(stateStr)
			}
			line += fmt.Sprintf(" %10s", stateStr)
		}
	}

	fmt.Println(line)
}

func printMonitorFooter(count int, weightMgr *weight.WeightManager, throttleMgr *throttle.ThrottleManager) {
	fmt.Println(strings.Repeat("-", 140))
	footer := fmt.Sprintf("共监控 %d 个容器 | 按 Ctrl+C 退出", count)

	if weightMgr != nil {
		elevated := len(weightMgr.GetElevatedContainers())
		if elevated > 0 {
			yellow := color.New(color.FgYellow, color.Bold).SprintFunc()
			footer += fmt.Sprintf(" | %s %d", yellow("已提升:"), elevated)
		}
	}

	if throttleMgr != nil {
		throttled := len(throttleMgr.GetThrottledContainers())
		if throttled > 0 {
			magenta := color.New(color.FgMagenta, color.Bold).SprintFunc()
			footer += fmt.Sprintf(" | %s %d", magenta("限速中:"), throttled)
		}
	}

	fmt.Println(footer)
}

func formatThrottleState(state throttle.ThrottleState) string {
	switch state {
	case throttle.StateNormal:
		return "normal"
	case throttle.StateThrottled:
		return "THROTTLED"
	case throttle.StateRecovering:
		return "recovering"
	default:
		return "unknown"
	}
}

func formatWeightState(state weight.WeightState) string {
	switch state {
	case weight.StateNormal:
		return "normal"
	case weight.StateElevated:
		return "ELEVATED"
	case weight.StateRecovering:
		return "recovering"
	default:
		return "unknown"
	}
}
