package predict

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"
)

type DirtyRateSample struct {
	Timestamp    time.Time `json:"timestamp"`
	TotalMemory  int64     `json:"total_memory_bytes"`
	DirtyMemory  int64     `json:"dirty_memory_bytes"`
	DirtyRate    float64   `json:"dirty_rate_bytes_per_sec"`
	SamplePeriod float64   `json:"sample_period_sec"`
}

type DirtyRateResult struct {
	ContainerName  string            `json:"container_name"`
	Samples        []DirtyRateSample `json:"samples"`
	AvgDirtyRate   float64           `json:"avg_dirty_rate_bytes_per_sec"`
	PeakDirtyRate  float64           `json:"peak_dirty_rate_bytes_per_sec"`
	MinDirtyRate   float64           `json:"min_dirty_rate_bytes_per_sec"`
	StdDev         float64           `json:"std_dev_bytes_per_sec"`
	Confidence     float64           `json:"confidence"`
	TotalMemory    int64             `json:"total_memory_bytes"`
}

func MeasureDirtyRate(containerName string, sampleCount int, sampleInterval time.Duration) (*DirtyRateResult, error) {
	if sampleCount < 2 {
		sampleCount = 3
	}
	if sampleInterval < 1*time.Second {
		sampleInterval = 2 * time.Second
	}

	result := &DirtyRateResult{
		ContainerName: containerName,
	}

	prevDirtyPages := int64(0)
	prevTime := time.Time{}

	for i := 0; i < sampleCount; i++ {
		if i > 0 {
			time.Sleep(sampleInterval)
		}

		dirtyPages, totalMem, err := getContainerDirtyPages(containerName)
		if err != nil {
			return nil, fmt.Errorf("采样 %d 失败: %w", i+1, err)
		}

		now := time.Now()
		sample := DirtyRateSample{
			Timestamp:   now,
			TotalMemory: totalMem,
			DirtyMemory: dirtyPages,
		}

		if !prevTime.IsZero() {
			elapsed := now.Sub(prevTime).Seconds()
			if elapsed > 0 {
				sample.SamplePeriod = elapsed
				sample.DirtyRate = float64(dirtyPages-prevDirtyPages) / elapsed
				if sample.DirtyRate < 0 {
					sample.DirtyRate = 0
				}
			}
		}

		result.Samples = append(result.Samples, sample)
		prevDirtyPages = dirtyPages
		prevTime = now
	}

	result.calculateStats()
	return result, nil
}

func getContainerDirtyPages(containerName string) (int64, int64, error) {
	cmd := exec.Command("lxc-cgroup", "-n", containerName, "memory.usage_in_bytes")
	output, err := cmd.Output()
	if err != nil {
		return 0, 0, fmt.Errorf("获取内存使用失败: %w", err)
	}
	totalMem, _ := strconv.ParseInt(strings.TrimSpace(string(output)), 10, 64)

	dirtyPages, err := getDirtyPagesViaProc(containerName)
	if err != nil {
		dirtyPages = estimateDirtyPages(containerName, totalMem)
	}

	return dirtyPages, totalMem, nil
}

func getDirtyPagesViaProc(containerName string) (int64, error) {
	infoCmd := exec.Command("lxc-info", "-n", containerName, "-H", "-p")
	output, err := infoCmd.Output()
	if err != nil {
		return 0, err
	}

	var pid int
	for _, line := range strings.Split(string(output), "\n") {
		parts := strings.SplitN(line, ":", 2)
		if len(parts) == 2 && strings.TrimSpace(parts[0]) == "pid" {
			pid, _ = strconv.Atoi(strings.TrimSpace(parts[1]))
			break
		}
	}

	if pid == 0 {
		return 0, fmt.Errorf("无法获取容器PID")
	}

	smapsPath := fmt.Sprintf("/proc/%d/smaps_rollup", pid)
	data, err := os.ReadFile(smapsPath)
	if err != nil {
		return 0, fmt.Errorf("读取smaps失败: %w", err)
	}

	var dirtyPages int64
	for _, line := range strings.Split(string(data), "\n") {
		if strings.HasPrefix(line, "Anonymous:") {
			parts := strings.Fields(line)
			if len(parts) >= 2 {
				val, _ := strconv.ParseInt(parts[1], 10, 64)
				dirtyPages += val * 1024
			}
		}
		if strings.HasPrefix(line, "Private_Dirty:") {
			parts := strings.Fields(line)
			if len(parts) >= 2 {
				val, _ := strconv.ParseInt(parts[1], 10, 64)
				dirtyPages += val * 1024
			}
		}
	}

	return dirtyPages, nil
}

func estimateDirtyPages(containerName string, totalMem int64) int64 {
	infoCmd := exec.Command("lxc-info", "-n", containerName, "-H", "-p")
	output, err := infoCmd.Output()
	if err != nil {
		return totalMem / 4
	}

	var pid int
	for _, line := range strings.Split(string(output), "\n") {
		parts := strings.SplitN(line, ":", 2)
		if len(parts) == 2 && strings.TrimSpace(parts[0]) == "pid" {
			pid, _ = strconv.Atoi(strings.TrimSpace(parts[1]))
			break
		}
	}

	if pid == 0 {
		return totalMem / 4
	}

	statmPath := fmt.Sprintf("/proc/%d/statm", pid)
	data, err := os.ReadFile(statmPath)
	if err != nil {
		return totalMem / 4
	}

	parts := strings.Fields(string(data))
	if len(parts) >= 3 {
		resident, _ := strconv.ParseInt(parts[1], 10, 64)
		shared, _ := strconv.ParseInt(parts[2], 10, 64)
		pageSize := int64(4096)
		dirtyEstimate := (resident - shared) * pageSize
		if dirtyEstimate < 0 {
			dirtyEstimate = totalMem / 4
		}
		return dirtyEstimate
	}

	return totalMem / 4
}

func (r *DirtyRateResult) calculateStats() {
	if len(r.Samples) < 2 {
		return
	}

	var sum, sumSq float64
	var count int
	r.PeakDirtyRate = 0
	r.MinDirtyRate = 1e18

	for _, s := range r.Samples {
		if s.DirtyRate > 0 {
			sum += s.DirtyRate
			sumSq += s.DirtyRate * s.DirtyRate
			count++
			if s.DirtyRate > r.PeakDirtyRate {
				r.PeakDirtyRate = s.DirtyRate
			}
			if s.DirtyRate < r.MinDirtyRate {
				r.MinDirtyRate = s.DirtyRate
			}
		}
	}

	if count > 0 {
		r.AvgDirtyRate = sum / float64(count)
		variance := (sumSq / float64(count)) - (r.AvgDirtyRate * r.AvgDirtyRate)
		if variance < 0 {
			variance = 0
		}
		r.StdDev = sqrt(variance)

		mean := r.AvgDirtyRate
		cv := 0.0
		if mean > 0 {
			cv = r.StdDev / mean
		}
		r.Confidence = 1.0 - cv
		if r.Confidence < 0 {
			r.Confidence = 0
		}
		if r.Confidence > 1 {
			r.Confidence = 1
		}
	}

	if len(r.Samples) > 0 {
		r.TotalMemory = r.Samples[0].TotalMemory
	}
}

func sqrt(x float64) float64 {
	if x <= 0 {
		return 0
	}
	z := x
	for i := 0; i < 10; i++ {
		z = (z + x/z) / 2
	}
	return z
}

func QuickDirtyRateEstimate(containerName string) (float64, int64, error) {
	result, err := MeasureDirtyRate(containerName, 2, 2*time.Second)
	if err != nil {
		return 0, 0, err
	}
	return result.AvgDirtyRate, result.TotalMemory, nil
}

func (r *DirtyRateResult) ToJSON() ([]byte, error) {
	return json.MarshalIndent(r, "", "  ")
}
