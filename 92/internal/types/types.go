package types

import (
	"fmt"
	"time"
)

type IOLimit struct {
	ReadBPS   int64  `yaml:"read_bps" json:"read_bps"`
	WriteBPS  int64  `yaml:"write_bps" json:"write_bps"`
	ReadIOPS  int64  `yaml:"read_iops" json:"read_iops"`
	WriteIOPS int64  `yaml:"write_iops" json:"write_iops"`
	Priority  string `yaml:"priority" json:"priority"`
}

type ContainerConfig struct {
	ContainerID   string  `yaml:"container_id" json:"container_id"`
	ContainerName string  `yaml:"container_name,omitempty" json:"container_name,omitempty"`
	CgroupPath    string  `yaml:"cgroup_path,omitempty" json:"cgroup_path,omitempty"`
	Limits        IOLimit `yaml:"limits" json:"limits"`
}

type IOStats struct {
	ContainerID   string
	ContainerName string
	ReadBytes     uint64
	WriteBytes    uint64
	ReadIOPS      float64
	WriteIOPS     float64
	ReadBPS       float64
	WriteBPS      float64
	QueueLength   uint64
	WaitTime      uint64
	Timestamp     time.Time
}

type DeviceInfo struct {
	Major int
	Minor int
	Name  string
}

type ConfigFile struct {
	Version  string            `yaml:"version"`
	Defaults *IOLimit          `yaml:"defaults,omitempty"`
	Rules    []ContainerConfig `yaml:"rules"`
}

type RollbackPoint struct {
	Timestamp     time.Time
	PreviousState map[string]IOLimit
	AppliedRules  []ContainerConfig
}

type StarvationEvent struct {
	ContainerID    string    `json:"container_id"`
	ContainerName  string    `json:"container_name"`
	StartTime      time.Time `json:"start_time"`
	EndTime        time.Time `json:"end_time,omitempty"`
	WaitTimeMs     uint64    `json:"wait_time_ms"`
	QueueLength    uint64    `json:"queue_length"`
	OriginalWeight int       `json:"original_weight"`
	ElevatedWeight int       `json:"elevated_weight"`
	Resolved       bool      `json:"resolved"`
}

type WeightAdjustmentHistory struct {
	ContainerID    string        `json:"container_id"`
	ContainerName  string        `json:"container_name"`
	Adjustments    []Adjustment  `json:"adjustments"`
}

type Adjustment struct {
	Timestamp      time.Time `json:"timestamp"`
	Action         string    `json:"action"`
	FromWeight     int       `json:"from_weight"`
	ToWeight       int       `json:"to_weight"`
	Reason         string    `json:"reason"`
	DurationSec    float64   `json:"duration_sec,omitempty"`
}

type LatencyInfo struct {
	ReadLatency   float64 `json:"read_latency"`
	WriteLatency  float64 `json:"write_latency"`
	AvgLatency    float64 `json:"avg_latency"`
	QueueLength   uint64  `json:"queue_length"`
	IOUtilization float64 `json:"io_utilization"`
}

type LatencyRecord struct {
	Timestamp    time.Time `json:"timestamp"`
	DeviceName   string    `json:"device_name"`
	ReadLatency  float64   `json:"read_latency"`
	WriteLatency float64   `json:"write_latency"`
	AvgLatency   float64   `json:"avg_latency"`
}

type LatencySummary struct {
	DeviceName  string    `json:"device_name"`
	Current     float64   `json:"current"`
	Avg5Min     float64   `json:"avg_5min"`
	Min5Min     float64   `json:"min_5min"`
	Max5Min     float64   `json:"max_5min"`
	Trend       string    `json:"trend"`
	Predictions []float64 `json:"predictions"`
}

type LatencyMonitor struct {
	ThresholdMs  float64       `json:"threshold_ms"`
	Duration     time.Duration `json:"duration"`
	MinBandwidth int64         `json:"min_bandwidth"`
	MinIOPS      int64         `json:"min_iops"`
}

type ThrottleState int

const (
	ThrottleStateNormal ThrottleState = iota
	ThrottleStateThrottled
	ThrottleStateRecovering
)

type ThrottleEvent struct {
	ContainerID   string    `json:"container_id"`
	ContainerName string    `json:"container_name"`
	Timestamp     time.Time `json:"timestamp"`
	EventType     string    `json:"event_type"`
	OriginalBPS   int64     `json:"original_bps"`
	ReducedBPS    int64     `json:"reduced_bps"`
	OriginalIOPS  int64     `json:"original_iops"`
	ReducedIOPS   int64     `json:"reduced_iops"`
	Reason        string    `json:"reason"`
}

type ThrottleConfig struct {
	Enabled        bool          `json:"enabled"`
	ThresholdMs    float64       `json:"threshold_ms"`
	ThresholdDur   time.Duration `json:"threshold_dur"`
	RecoveryDur    time.Duration `json:"recovery_dur"`
	ReductionRatio float64       `json:"reduction_ratio"`
	MinBPS         int64         `json:"min_bps"`
	MinIOPS        int64         `json:"min_iops"`
}

type WebDashboardConfig struct {
	Enabled bool   `json:"enabled"`
	Port    int    `json:"port"`
	Refresh int    `json:"refresh"`
}

const (
	PriorityHigh   = "high"
	PriorityMedium = "medium"
	PriorityLow    = "low"
)

func GetPriorityWeight(priority string) int {
	switch priority {
	case PriorityHigh:
		return 100
	case PriorityMedium:
		return 50
	case PriorityLow:
		return 10
	default:
		return 50
	}
}

func ParseSize(sizeStr string) (int64, error) {
	if sizeStr == "" {
		return 0, nil
	}
	var multiplier int64 = 1
	if len(sizeStr) >= 2 {
		suffix := sizeStr[len(sizeStr)-2:]
		numStr := sizeStr[:len(sizeStr)-2]
		switch suffix {
		case "KB", "kb":
			multiplier = 1024
			sizeStr = numStr
		case "MB", "mb":
			multiplier = 1024 * 1024
			sizeStr = numStr
		case "GB", "gb":
			multiplier = 1024 * 1024 * 1024
			sizeStr = numStr
		case "TB", "tb":
			multiplier = 1024 * 1024 * 1024 * 1024
			sizeStr = numStr
		}
	}
	var value int64
	_, err := fmt.Sscanf(sizeStr, "%d", &value)
	if err != nil {
		return 0, err
	}
	return value * multiplier, nil
}
