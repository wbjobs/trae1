package stats

import (
	"context"
	"sync"
	"time"

	"github.com/shirou/gopsutil/v3/cpu"
	"github.com/shirou/gopsutil/v3/mem"
	"go.uber.org/zap"
)

type ResourceStats struct {
	mu sync.RWMutex

	CPUTotal    float64
	CPUPercent  float64
	MemoryUsed  uint64
	MemoryMax   uint64
	IOReadBytes uint64
	IOWriteBytes uint64
	IOReadRate  float64
	IOWriteRate float64
	StartTime   time.Time
	EndTime     time.Time
	Samples     int
}

type Collector struct {
	logger *zap.Logger
}

func NewCollector(logger *zap.Logger) *Collector {
	return &Collector{
		logger: logger,
	}
}

func NewResourceStats() *ResourceStats {
	return &ResourceStats{
		StartTime: time.Now(),
	}
}

func (rs *ResourceStats) Clone() *ResourceStats {
	rs.mu.RLock()
	defer rs.mu.RUnlock()

	return &ResourceStats{
		CPUTotal:     rs.CPUTotal,
		CPUPercent:   rs.CPUPercent,
		MemoryUsed:   rs.MemoryUsed,
		MemoryMax:    rs.MemoryMax,
		IOReadBytes:  rs.IOReadBytes,
		IOWriteBytes: rs.IOWriteBytes,
		IOReadRate:   rs.IOReadRate,
		IOWriteRate:  rs.IOWriteRate,
		StartTime:    rs.StartTime,
		EndTime:      rs.EndTime,
		Samples:      rs.Samples,
	}
}

func (rs *ResourceStats) Update(cpuPercent float64, memUsed uint64, ioReadBytes, ioWriteBytes uint64, duration time.Duration) {
	rs.mu.Lock()
	defer rs.mu.Unlock()

	rs.CPUPercent = cpuPercent
	rs.MemoryUsed = memUsed
	rs.IOReadBytes = ioReadBytes
	rs.IOWriteBytes = ioWriteBytes
	rs.EndTime = time.Now()
	rs.Samples++

	if memUsed > rs.MemoryMax {
		rs.MemoryMax = memUsed
	}

	if duration.Seconds() > 0 {
		rs.CPUTotal = cpuPercent * duration.Seconds()
		rs.IOReadRate = float64(ioReadBytes) / duration.Seconds()
		rs.IOWriteRate = float64(ioWriteBytes) / duration.Seconds()
	}
}

func (c *Collector) StartCollecting(ctx context.Context, stats *ResourceStats, rootDir string) {
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()

	startTime := time.Now()
	var lastReadBytes, lastWriteBytes uint64

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			cpuPercent, err := cpu.Percent(0, false)
			if err != nil {
				c.logger.Debug("Failed to get CPU percent", zap.Error(err))
				continue
			}

			memInfo, err := mem.VirtualMemory()
			if err != nil {
				c.logger.Debug("Failed to get memory info", zap.Error(err))
				continue
			}

			ioStats := c.getIOStats(rootDir)

			duration := time.Since(startTime)

			stats.Update(
				cpuPercent[0],
				memInfo.Used,
				ioStats.ReadBytes,
				ioStats.WriteBytes,
				duration,
			)

			lastReadBytes = ioStats.ReadBytes
			lastWriteBytes = ioStats.WriteBytes
		}
	}
}

type IOStats struct {
	ReadBytes  uint64
	WriteBytes uint64
}

func (c *Collector) getIOStats(rootDir string) IOStats {
	return IOStats{
		ReadBytes:  0,
		WriteBytes: 0,
	}
}
