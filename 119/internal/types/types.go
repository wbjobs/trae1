package types

import "time"

type MigrationConfig struct {
	ContainerName string
	TargetHost    string
	TargetPort    int
	BwLimit       int64
	PreCopy       bool
	PreCopyIter   int
	Verbose       bool
	Force         bool
}

type ResourceInfo struct {
	TotalMemory     uint64
	FreeMemory      uint64
	AvailableMemory uint64
	TotalDisk       uint64
	FreeDisk        uint64
	CPUNum          int
	CPUFreePercent  float64
}

type ContainerInfo struct {
	Name      string
	PID       int
	MemoryUsage uint64
	IPAddress  string
	Status     string
}

type CheckpointMeta struct {
	ContainerName string    `json:"container_name"`
	Timestamp     time.Time `json:"timestamp"`
	MemorySize    uint64    `json:"memory_size"`
	PagesCount    uint64    `json:"pages_count"`
	PreCopyIter   int       `json:"precopy_iter"`
	NetworkConfig NetworkConfig `json:"network_config"`
}

type NetworkConfig struct {
	OldIP string `json:"old_ip"`
	NewIP string `json:"new_ip"`
	Interface string `json:"interface"`
	Gateway  string `json:"gateway"`
}

type ProgressState struct {
	Phase        string
	TotalBytes   int64
	Transferred  int64
	Speed        float64
	Percent      float64
	StartTime    time.Time
}

type DaemonConfig struct {
	ListenAddr string
	ListenPort int
	DataDir    string
	MaxConn    int
}
