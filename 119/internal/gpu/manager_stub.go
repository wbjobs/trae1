//go:build !linux

package gpu

import (
	"fmt"
	"time"
)

type GPUDevice struct {
	ID          int    `json:"id"`
	UUID        string `json:"uuid"`
	Name        string `json:"name"`
	MemoryMB    uint64 `json:"memory_mb"`
	FreeMemoryMB uint64 `json:"free_memory_mb"`
	MIGEnabled  bool   `json:"mig_enabled"`
	MIGMode     string `json:"mig_mode"`
	BusID       string `json:"bus_id"`
	ComputeMode string `json:"compute_mode"`
}

type MIGDevice struct {
	GPUID       int    `json:"gpu_id"`
	GIID        int    `json:"gi_id"`
	CIID        int    `json:"ci_id"`
	UUID        string `json:"uuid"`
	Profile     string `json:"profile"`
	MemoryMB    uint64 `json:"memory_mb"`
	Name        string `json:"name"`
}

type GPUDeviceMapping struct {
	SourceDeviceUUID string `json:"source_uuid"`
	TargetDeviceUUID string `json:"target_uuid"`
	SourceDeviceID   int    `json:"source_id"`
	TargetDeviceID   int    `json:"target_id"`
	IsMIG            bool   `json:"is_mig"`
	MIGProfile       string `json:"mig_profile,omitempty"`
}

type GPUState struct {
	Devices         []GPUDevice      `json:"devices"`
	MIGDevices      []MIGDevice      `json:"mig_devices"`
	DeviceMappings  []GPUDeviceMapping `json:"device_mappings"`
	Processes       []GPUProcess     `json:"processes"`
	MemoryBuffers   []GPUMemoryBuffer `json:"memory_buffers"`
	SignalTimeout   time.Duration    `json:"signal_timeout"`
}

type GPUProcess struct {
	PID          int    `json:"pid"`
	GPUUUID      string `json:"gpu_uuid"`
	Name         string `json:"name"`
	MemoryUsedMB uint64 `json:"memory_used_mb"`
	GPUs         []int  `json:"gpus"`
}

type GPUMemoryBuffer struct {
	SourceAddr uint64 `json:"source_addr"`
	TargetAddr uint64 `json:"target_addr"`
	SizeBytes  uint64 `json:"size_bytes"`
	DeviceUUID string `json:"device_uuid"`
	HostBuffer []byte `json:"-"`
}

type GPUManager struct {
	nvidiaSMIPath string
	cudaPath      string
	verbose       bool
}

func NewGPUManager(verbose bool) *GPUManager {
	return &GPUManager{
		nvidiaSMIPath: "",
		cudaPath:      "",
		verbose:       verbose,
	}
}

func (m *GPUManager) IsAvailable() bool {
	return false
}

func (m *GPUManager) IsCUDAAvailable() bool {
	return false
}

func (m *GPUManager) GetGPUDevices() ([]GPUDevice, error) {
	return nil, fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) GetGPUProcesses(containerPID int) ([]GPUProcess, error) {
	return nil, fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) GetMIGDevices() ([]MIGDevice, error) {
	return nil, fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) SendGPUSignal(pid int, signal interface{}) error {
	return fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) NotifyGPURelease(pid int, timeout time.Duration) error {
	return fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) NotifyGPURestore(pid int) error {
	return fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) TestGPUAvailability(deviceUUID string) error {
	return fmt.Errorf("GPU management only available on Linux")
}

func (m *GPUManager) CheckTargetGPUMemory(sourceGPUs []GPUDevice, targetGPUs []GPUDevice, deviceMappings []GPUDeviceMapping) error {
	return fmt.Errorf("GPU management only available on Linux")
}
