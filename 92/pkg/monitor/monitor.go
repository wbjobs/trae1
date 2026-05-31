package monitor

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
)

type Monitor struct {
	ioCtl       *cgroup.IOController
	interval    time.Duration
	prevStats   map[string]map[types.DeviceInfo]types.IOStats
	statsLock   sync.Mutex
	stopChan    chan struct{}
	cgroupRoot  string
}

func NewMonitor(interval time.Duration) *Monitor {
	return &Monitor{
		ioCtl:      cgroup.NewIOController(),
		interval:   interval,
		prevStats:  make(map[string]map[types.DeviceInfo]types.IOStats),
		stopChan:   make(chan struct{}),
		cgroupRoot: "/sys/fs/cgroup",
	}
}

func NewMonitorWithRoot(interval time.Duration, cgroupRoot string) *Monitor {
	return &Monitor{
		ioCtl:      cgroup.NewIOControllerWithRoot(cgroupRoot),
		interval:   interval,
		prevStats:  make(map[string]map[types.DeviceInfo]types.IOStats),
		stopChan:   make(chan struct{}),
		cgroupRoot: cgroupRoot,
	}
}

func (m *Monitor) GetStats(cgroupPath, containerID, containerName string) (*types.IOStats, error) {
	statsMap, err := m.ioCtl.GetIOStats(cgroupPath)
	if err != nil {
		return nil, err
	}

	if len(statsMap) == 0 {
		return nil, fmt.Errorf("no io stats available")
	}

	var totalStats types.IOStats
	var firstDev types.DeviceInfo

	for dev, stat := range statsMap {
		firstDev = dev
		totalStats.ReadBytes += stat.ReadBytes
		totalStats.WriteBytes += stat.WriteBytes
		totalStats.ReadIOPS += stat.ReadIOPS
		totalStats.WriteIOPS += stat.WriteIOPS
		break
	}

	queueLen, waitTime, _ := m.getPressureStats(cgroupPath)
	totalStats.QueueLength = queueLen
	totalStats.WaitTime = waitTime

	m.statsLock.Lock()
	prev, hasPrev := m.prevStats[cgroupPath]
	m.statsLock.Unlock()

	if hasPrev {
		if prevStat, ok := prev[firstDev]; ok {
			elapsed := totalStats.Timestamp.Sub(prevStat.Timestamp).Seconds()
			if elapsed > 0 {
				totalStats.ReadBPS = float64(totalStats.ReadBytes-prevStat.ReadBytes) / elapsed
				totalStats.WriteBPS = float64(totalStats.WriteBytes-prevStat.WriteBytes) / elapsed
				totalStats.ReadIOPS = (totalStats.ReadIOPS - prevStat.ReadIOPS) / elapsed
				totalStats.WriteIOPS = (totalStats.WriteIOPS - prevStat.WriteIOPS) / elapsed

				if totalStats.ReadBPS < 0 {
					totalStats.ReadBPS = 0
				}
				if totalStats.WriteBPS < 0 {
					totalStats.WriteBPS = 0
				}
				if totalStats.ReadIOPS < 0 {
					totalStats.ReadIOPS = 0
				}
				if totalStats.WriteIOPS < 0 {
					totalStats.WriteIOPS = 0
				}
			}
		}
	}

	m.statsLock.Lock()
	m.prevStats[cgroupPath] = statsMap
	m.statsLock.Unlock()

	totalStats.ContainerID = containerID
	totalStats.ContainerName = containerName

	return &totalStats, nil
}

func (m *Monitor) getPressureStats(cgroupPath string) (uint64, uint64, error) {
	fullPath := filepath.Join(m.cgroupRoot, cgroupPath, "io.pressure")
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return 0, 0, err
	}

	var queueLength, waitTime uint64
	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.Fields(line)
		if len(parts) < 2 {
			continue
		}

		for _, part := range parts[1:] {
			kv := strings.Split(part, "=")
			if len(kv) != 2 {
				continue
			}
			key := kv[0]
			val, err := strconv.ParseUint(kv[1], 10, 64)
			if err != nil {
				continue
			}

			switch key {
			case "avg10":
				queueLength = val
			case "total":
				waitTime = val
			}
		}
	}

	return queueLength, waitTime, nil
}

func (m *Monitor) GetQueueLength(cgroupPath string) (uint64, error) {
	fullPath := filepath.Join(m.cgroupRoot, cgroupPath, "io.stat")
	content, err := os.ReadFile(fullPath)
	if err != nil {
		return 0, err
	}

	var queue uint64
	scanner := bufio.NewScanner(strings.NewReader(string(content)))
	for scanner.Scan() {
		line := scanner.Text()
		parts := strings.Fields(line)
		for _, part := range parts[1:] {
			kv := strings.Split(part, "=")
			if len(kv) != 2 {
				continue
			}
			if kv[0] == "wait" {
				val, err := strconv.ParseUint(kv[1], 10, 64)
				if err == nil {
					queue += val
				}
			}
		}
	}

	return queue, nil
}

type StatsCollector struct {
	monitor     *Monitor
	containers  []ContainerTarget
	statsChan   chan []types.IOStats
	errorChan   chan error
	wg          sync.WaitGroup
	running     bool
}

type ContainerTarget struct {
	ContainerID   string
	ContainerName string
	CgroupPath    string
}

func NewStatsCollector(monitor *Monitor, containers []ContainerTarget) *StatsCollector {
	return &StatsCollector{
		monitor:    monitor,
		containers: containers,
		statsChan:  make(chan []types.IOStats, 100),
		errorChan:  make(chan error, 100),
	}
}

func (sc *StatsCollector) Start() {
	if sc.running {
		return
	}
	sc.running = true

	sc.wg.Add(1)
	go func() {
		defer sc.wg.Done()
		ticker := time.NewTicker(sc.monitor.interval)
		defer ticker.Stop()

		for {
			select {
			case <-sc.monitor.stopChan:
				return
			case <-ticker.C:
				stats := sc.collectOnce()
				if len(stats) > 0 {
					sc.statsChan <- stats
				}
			}
		}
	}()
}

func (sc *StatsCollector) collectOnce() []types.IOStats {
	var stats []types.IOStats

	for _, c := range sc.containers {
		stat, err := sc.monitor.GetStats(c.CgroupPath, c.ContainerID, c.ContainerName)
		if err != nil {
			sc.errorChan <- fmt.Errorf("container %s: %w", c.ContainerID, err)
			continue
		}
		stats = append(stats, *stat)
	}

	return stats
}

func (sc *StatsCollector) Stop() {
	if !sc.running {
		return
	}
	sc.running = false
	close(sc.monitor.stopChan)
	sc.wg.Wait()
	close(sc.statsChan)
	close(sc.errorChan)
}

func (sc *StatsCollector) StatsChan() <-chan []types.IOStats {
	return sc.statsChan
}

func (sc *StatsCollector) ErrorChan() <-chan error {
	return sc.errorChan
}

func FormatBytes(bytes float64) string {
	units := []string{"B", "KB", "MB", "GB", "TB"}
	unit := 0
	val := bytes

	for val >= 1024 && unit < len(units)-1 {
		val /= 1024
		unit++
	}

	return fmt.Sprintf("%.2f %s", val, units[unit])
}

func FormatIOPS(iops float64) string {
	if iops >= 1000000 {
		return fmt.Sprintf("%.2f M", iops/1000000)
	} else if iops >= 1000 {
		return fmt.Sprintf("%.2f K", iops/1000)
	}
	return fmt.Sprintf("%.0f", iops)
}

func FormatBandwidth(bps float64) string {
	return FormatBytes(bps) + "/s"
}
