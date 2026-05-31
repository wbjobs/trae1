package latency

import (
	"bufio"
	"fmt"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/io-qos/io-qos/internal/types"
)

type DiskStats struct {
	DeviceName     string
	Major          int
	Minor          int
	ReadIOs        uint64
	ReadMerges     uint64
	ReadSectors    uint64
	ReadTicks      uint64
	WriteIOs       uint64
	WriteMerges    uint64
	WriteSectors   uint64
	WriteTicks     uint64
	InFlight       uint64
	IoTicks        uint64
	TimeInQueue    uint64
	DiscardIOs     uint64
	DiscardMerges  uint64
	DiscardSectors uint64
	DiscardTicks   uint64
	Timestamp      time.Time
}

type LatencyMonitor struct {
	mu             sync.Mutex
	stats          map[string][]DiskStats
	latencyHistory map[string][]types.LatencyRecord
	maxHistorySize int
	interval       time.Duration
	stopChan       chan struct{}
	running        bool
}

func NewLatencyMonitor(interval time.Duration) *LatencyMonitor {
	return &LatencyMonitor{
		stats:          make(map[string][]DiskStats),
		latencyHistory: make(map[string][]types.LatencyRecord),
		maxHistorySize: 3600,
		interval:       interval,
		stopChan:       make(chan struct{}),
	}
}

func (lm *LatencyMonitor) Start() {
	if lm.running {
		return
	}
	lm.running = true

	go func() {
		ticker := time.NewTicker(lm.interval)
		defer ticker.Stop()

		for {
			select {
			case <-lm.stopChan:
				lm.running = false
				return
			case <-ticker.C:
				lm.collectStats()
			}
		}
	}()
}

func (lm *LatencyMonitor) Stop() {
	if lm.running {
		close(lm.stopChan)
		lm.running = false
	}
}

func (lm *LatencyMonitor) collectStats() {
	file, err := os.Open("/proc/diskstats")
	if err != nil {
		return
	}
	defer file.Close()

	lm.mu.Lock()
	defer lm.mu.Unlock()

	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		fields := strings.Fields(line)
		if len(fields) < 14 {
			continue
		}

		stats := lm.parseDiskStats(fields)
		if stats == nil {
			continue
		}

		device := stats.DeviceName
		lm.stats[device] = append(lm.stats[device], *stats)
		if len(lm.stats[device]) > 2 {
			lm.stats[device] = lm.stats[device][len(lm.stats[device])-2:]
		}

		if len(lm.stats[device]) >= 2 {
			latency := lm.calculateLatency(device)
			record := types.LatencyRecord{
				Timestamp:  time.Now(),
				DeviceName: device,
				ReadLatency:  latency.ReadLatency,
				WriteLatency: latency.WriteLatency,
				AvgLatency:   latency.AvgLatency,
			}
			lm.latencyHistory[device] = append(lm.latencyHistory[device], record)
			if len(lm.latencyHistory[device]) > lm.maxHistorySize {
				lm.latencyHistory[device] = lm.latencyHistory[device][len(lm.latencyHistory[device])-lm.maxHistorySize:]
			}
		}
	}
}

func (lm *LatencyMonitor) parseDiskStats(fields []string) *DiskStats {
	if len(fields) < 14 {
		return nil
	}

	parseUint := func(s string) uint64 {
		v, _ := strconv.ParseUint(s, 10, 64)
		return v
	}

	parseInt := func(s string) int {
		v, _ := strconv.Atoi(s)
		return v
	}

	stats := &DiskStats{
		Major:        parseInt(fields[0]),
		Minor:        parseInt(fields[1]),
		DeviceName:   fields[2],
		ReadIOs:      parseUint(fields[3]),
		ReadMerges:   parseUint(fields[4]),
		ReadSectors:  parseUint(fields[5]),
		ReadTicks:    parseUint(fields[6]),
		WriteIOs:     parseUint(fields[7]),
		WriteMerges:  parseUint(fields[8]),
		WriteSectors: parseUint(fields[9]),
		WriteTicks:   parseUint(fields[10]),
		InFlight:     parseUint(fields[11]),
		IoTicks:      parseUint(fields[12]),
		TimeInQueue:  parseUint(fields[13]),
		Timestamp:    time.Now(),
	}

	if len(fields) >= 18 {
		stats.DiscardIOs = parseUint(fields[14])
		stats.DiscardMerges = parseUint(fields[15])
		stats.DiscardSectors = parseUint(fields[16])
		stats.DiscardTicks = parseUint(fields[17])
	}

	return stats
}

func (lm *LatencyMonitor) calculateLatency(device string) *types.LatencyInfo {
	stats := lm.stats[device]
	if len(stats) < 2 {
		return &types.LatencyInfo{ReadLatency: 0, WriteLatency: 0, AvgLatency: 0}
	}

	prev := stats[len(stats)-2]
	curr := stats[len(stats)-1]

	readIOsDiff := curr.ReadIOs - prev.ReadIOs
	writeIOsDiff := curr.WriteIOs - prev.WriteIOs
	readTicksDiff := curr.ReadTicks - prev.ReadTicks
	writeTicksDiff := curr.WriteTicks - prev.WriteTicks

	var readLatency, writeLatency float64

	if readIOsDiff > 0 {
		readLatency = float64(readTicksDiff) / float64(readIOsDiff)
	}
	if writeIOsDiff > 0 {
		writeLatency = float64(writeTicksDiff) / float64(writeIOsDiff)
	}

	avgLatency := readLatency
	if writeLatency > 0 {
		if readLatency > 0 {
			avgLatency = (readLatency + writeLatency) / 2
		} else {
			avgLatency = writeLatency
		}
	}

	return &types.LatencyInfo{
		ReadLatency:  readLatency,
		WriteLatency: writeLatency,
		AvgLatency:   avgLatency,
		QueueLength:  curr.InFlight,
		IOUtilization: float64(curr.IoTicks-prev.IoTicks) / float64(curr.Timestamp.Sub(prev.Timestamp).Milliseconds()) * 100,
	}
}

func (lm *LatencyMonitor) GetCurrentLatency(device string) *types.LatencyInfo {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	return lm.calculateLatency(device)
}

func (lm *LatencyMonitor) GetLatencyHistory(device string, duration time.Duration) []types.LatencyRecord {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	history := lm.latencyHistory[device]
	if len(history) == 0 {
		return nil
	}

	cutoff := time.Now().Add(-duration)
	var result []types.LatencyRecord
	for i := len(history) - 1; i >= 0; i-- {
		if history[i].Timestamp.Before(cutoff) {
			break
		}
		result = append([]types.LatencyRecord{history[i]}, result...)
	}

	return result
}

func (lm *LatencyMonitor) GetAllDevices() []string {
	lm.mu.Lock()
	defer lm.mu.Unlock()

	var devices []string
	for device := range lm.latencyHistory {
		if !strings.HasPrefix(device, "loop") &&
			!strings.HasPrefix(device, "ram") &&
			!strings.HasPrefix(device, "sr") {
			devices = append(devices, device)
		}
	}
	return devices
}

func (lm *LatencyMonitor) IsHighLatency(device string, thresholdMs float64, duration time.Duration) bool {
	history := lm.GetLatencyHistory(device, duration)
	if len(history) == 0 {
		return false
	}

	for _, record := range history {
		if record.AvgLatency < thresholdMs {
			return false
		}
	}

	return true
}

func (lm *LatencyMonitor) PredictLatency(device string, steps int) []float64 {
	history := lm.GetLatencyHistory(device, 5*time.Minute)
	if len(history) < 5 {
		return nil
	}

	var predictions []float64
	var sum float64
	var count int

	windowSize := min(10, len(history))
	for i := len(history) - windowSize; i < len(history); i++ {
		sum += history[i].AvgLatency
		count++
	}

	avg := sum / float64(count)
	var trend float64
	if len(history) >= 20 {
		recent := history[len(history)-10:]
		older := history[len(history)-20 : len(history)-10]

		var recentAvg, olderAvg float64
		for _, r := range recent {
			recentAvg += r.AvgLatency
		}
		recentAvg /= 10
		for _, r := range older {
			olderAvg += r.AvgLatency
		}
		olderAvg /= 10
		trend = recentAvg - olderAvg
	}

	for i := 0; i < steps; i++ {
		pred := avg + trend*float64(i+1)/10
		if pred < 0 {
			pred = 0
		}
		predictions = append(predictions, pred)
	}

	return predictions
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func (lm *LatencyMonitor) GetLatencySummary(device string) *types.LatencySummary {
	history := lm.GetLatencyHistory(device, 5*time.Minute)
	if len(history) == 0 {
		return nil
	}

	var sum, min, max float64
	min = 1e9

	for _, r := range history {
		sum += r.AvgLatency
		if r.AvgLatency < min {
			min = r.AvgLatency
		}
		if r.AvgLatency > max {
			max = r.AvgLatency
		}
	}

	avg := sum / float64(len(history))
	current := lm.GetCurrentLatency(device)

	return &types.LatencySummary{
		DeviceName: device,
		Current:    current.AvgLatency,
		Avg5Min:    avg,
		Min5Min:    min,
		Max5Min:    max,
		Trend:      lm.getTrend(device),
		Predictions: lm.PredictLatency(device, 5),
	}
}

func (lm *LatencyMonitor) getTrend(device string) string {
	history := lm.GetLatencyHistory(device, 2*time.Minute)
	if len(history) < 10 {
		return "stable"
	}

	first := history[:5]
	last := history[len(history)-5:]

	var firstAvg, lastAvg float64
	for _, r := range first {
		firstAvg += r.AvgLatency
	}
	firstAvg /= 5

	for _, r := range last {
		lastAvg += r.AvgLatency
	}
	lastAvg /= 5

	diff := lastAvg - firstAvg
	if diff > 20 {
		return "rising"
	} else if diff < -20 {
		return "falling"
	}
	return "stable"
}

func (lm *LatencyMonitor) GetLatencyPercentiles(device string, duration time.Duration) map[string]float64 {
	history := lm.GetLatencyHistory(device, duration)
	if len(history) == 0 {
		return nil
	}

	var latencies []float64
	for _, r := range history {
		latencies = append(latencies, r.AvgLatency)
	}

	for i := 0; i < len(latencies); i++ {
		for j := i + 1; j < len(latencies); j++ {
			if latencies[j] < latencies[i] {
				latencies[i], latencies[j] = latencies[j], latencies[i]
			}
		}
	}

	n := len(latencies)
	return map[string]float64{
		"p50":  latencies[n*50/100],
		"p90":  latencies[n*90/100],
		"p95":  latencies[n*95/100],
		"p99":  latencies[n*99/100],
		"p999": latencies[n*999/1000],
	}
}
