//go:build linux

package gpu

import (
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"golang.org/x/sys/unix"
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
	mu            sync.Mutex
	verbose       bool
}

func NewGPUManager(verbose bool) *GPUManager {
	nvidiaSMI, _ := findNvidiaSMI()
	cudaPath, _ := findCUDAToolkit()

	return &GPUManager{
		nvidiaSMIPath: nvidiaSMI,
		cudaPath:      cudaPath,
		verbose:       verbose,
	}
}

func findNvidiaSMI() (string, error) {
	paths := []string{
		"/usr/bin/nvidia-smi",
		"/usr/local/nvidia/bin/nvidia-smi",
		"/opt/nvidia/bin/nvidia-smi",
	}

	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			return p, nil
		}
	}

	return "", fmt.Errorf("nvidia-smi not found")
}

func findCUDAToolkit() (string, error) {
	paths := []string{
		"/usr/local/cuda",
		"/opt/cuda",
	}

	for _, p := range paths {
		if _, err := os.Stat(p); err == nil {
			return p, nil
		}
	}

	return "", fmt.Errorf("CUDA toolkit not found")
}

func (m *GPUManager) IsAvailable() bool {
	_, err := os.Stat(m.nvidiaSMIPath)
	return err == nil
}

func (m *GPUManager) IsCUDAAvailable() bool {
	if m.cudaPath == "" {
		return false
	}
	cudaLib := fmt.Sprintf("%s/lib64/libcudart.so", m.cudaPath)
	_, err := os.Stat(cudaLib)
	return err == nil
}

func (m *GPUManager) GetGPUDevices() ([]GPUDevice, error) {
	if !m.IsAvailable() {
		return nil, fmt.Errorf("no NVIDIA GPU available (nvidia-smi not found)")
	}

	devices, err := m.detectGPUs()
	if err != nil {
		return nil, fmt.Errorf("detect GPUs: %w", err)
	}

	for i := range devices {
		mig, err := m.detectMIGDevices(devices[i].ID)
		if err == nil {
			devices[i].MIGEnabled = len(mig) > 0
			if devices[i].MIGEnabled {
				devices[i].MIGMode = getMIGMode(devices[i])
			}
		}
	}

	return devices, nil
}

func (m *GPUManager) detectGPUs() ([]GPUDevice, error) {
	devices := make([]GPUDevice, 0)

	output, err := m.runNvidiaSMI("--query-gpu=index,uuid,name,memory.total,memory.free,serial,pci.bus_id,compute_mode", "--format=csv,noheader")
	if err != nil {
		return nil, fmt.Errorf("nvidia-smi query: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		fields := strings.Split(line, ", ")
		if len(fields) < 8 {
			continue
		}

		id, _ := strconv.Atoi(fields[0])
		totalMem, _ := parseMemoryMB(fields[3])
		freeMem, _ := parseMemoryMB(fields[4])

		devices = append(devices, GPUDevice{
			ID:          id,
			UUID:        strings.TrimSpace(fields[1]),
			Name:        strings.TrimSpace(fields[2]),
			MemoryMB:    totalMem,
			FreeMemoryMB: freeMem,
			BusID:       strings.TrimSpace(fields[6]),
			ComputeMode: strings.TrimSpace(fields[7]),
		})
	}

	return devices, nil
}

func parseMemoryMB(memStr string) (uint64, error) {
	memStr = strings.TrimSpace(memStr)
	memStr = strings.ReplaceAll(memStr, "MiB", "")
	memStr = strings.ReplaceAll(memStr, "MB", "")
	memStr = strings.TrimSpace(memStr)
	return strconv.ParseUint(memStr, 10, 64)
}

func (m *GPUManager) detectMIGDevices(gpuID int) ([]MIGDevice, error) {
	migDevices := make([]MIGDevice, 0)

	output, err := m.runNvidiaSMI(fmt.Sprintf("--id=%d", gpuID), "--query-mig-mode=current", "--format=csv,noheader")
	if err != nil {
		return nil, err
	}

	if !strings.Contains(output, "Enabled") {
		return nil, nil
	}

	output, err = m.runNvidiaSMI("--query-compute-apps=pid,process_name,used_memory", "--format=csv,noheader")
	if err != nil {
		return nil, fmt.Errorf("query MIG devices: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		if strings.Contains(line, "MIG") {
			fields := strings.Split(line, ", ")
			if len(fields) >= 3 {
				migDevices = append(migDevices, MIGDevice{
					GPUID:    gpuID,
					UUID:     fields[0],
					Name:     fields[1],
					MemoryMB: 0,
				})
			}
		}
	}

	return migDevices, nil
}

func (m *GPUManager) GetGPUProcesses(containerPID int) ([]GPUProcess, error) {
	if !m.IsAvailable() {
		return nil, nil
	}

	processes := make([]GPUProcess, 0)

	output, err := m.runNvidiaSMI("--query-compute-apps=pid,process_name,used_memory,gpu_uuid", "--format=csv,noheader")
	if err != nil {
		return nil, fmt.Errorf("query compute apps: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		fields := strings.Split(line, ", ")
		if len(fields) < 4 {
			continue
		}

		pid, err := strconv.Atoi(fields[0])
		if err != nil {
			continue
		}

		if !isDescendantOf(containerPID, pid) {
			continue
		}

		memUsed, _ := parseMemoryMB(fields[2])

		processes = append(processes, GPUProcess{
			PID:          pid,
			GPUUUID:      fields[3],
			Name:         fields[1],
			MemoryUsedMB: memUsed,
		})
	}

	return processes, nil
}

func (m *GPUManager) GetMIGDevices() ([]MIGDevice, error) {
	if !m.IsAvailable() {
		return nil, fmt.Errorf("no NVIDIA GPU available")
	}

	migDevices := make([]MIGDevice, 0)

	output, err := m.runNvidiaSMI("--query-gpu=index,mig_mode.current", "--format=csv,noheader")
	if err != nil {
		return nil, fmt.Errorf("query MIG mode: %w", err)
	}

	lines := strings.Split(strings.TrimSpace(output), "\n")
	for _, line := range lines {
		fields := strings.Split(line, ", ")
		if len(fields) < 2 || fields[1] != "Enabled" {
			continue
		}

		gpuID, _ := strconv.Atoi(fields[0])
		mig, err := m.detectMIGDevices(gpuID)
		if err == nil {
			migDevices = append(migDevices, mig...)
		}
	}

	return migDevices, nil
}

func (m *GPUManager) SendGPUSignal(pid int, signal unix.Signal) error {
	if err := unix.Kill(pid, signal); err != nil {
		return fmt.Errorf("send signal %d to PID %d: %w", signal, pid, err)
	}
	return nil
}

func (m *GPUManager) NotifyGPURelease(pid int, timeout time.Duration) error {
	if m.verbose {
		fmt.Printf("[gpu] sending SIGUSR1 to PID %d to release GPU resources\n", pid)
	}

	if err := m.SendGPUSignal(pid, unix.SIGUSR1); err != nil {
		return fmt.Errorf("send SIGUSR1: %w", err)
	}

	if timeout > 0 {
		if err := waitForGPURelease(pid, timeout); err != nil {
			return fmt.Errorf("GPU release timeout: %w", err)
		}
	}

	return nil
}

func (m *GPUManager) NotifyGPURestore(pid int) error {
	if m.verbose {
		fmt.Printf("[gpu] sending SIGUSR2 to PID %d to restore GPU resources\n", pid)
	}

	if err := m.SendGPUSignal(pid, unix.SIGUSR2); err != nil {
		return fmt.Errorf("send SIGUSR2: %w", err)
	}
	return nil
}

func (m *GPUManager) runNvidiaSMI(args ...string) (string, error) {
	if m.nvidiaSMIPath == "" {
		return "", fmt.Errorf("nvidia-smi not available")
	}

	cmd := exec.Command(m.nvidiaSMIPath, args...)
	output, err := cmd.CombinedOutput()
	if err != nil {
		return string(output), fmt.Errorf("nvidia-smi %v: %w: %s", args, err, string(output))
	}

	return string(output), nil
}

func (m *GPUManager) TestGPUAvailability(deviceUUID string) error {
	if !m.IsAvailable() {
		return fmt.Errorf("no NVIDIA GPU available")
	}

	args := []string{"--query-gpu=index,uuid,memory.free", "--format=csv,noheader"}
	if deviceUUID != "" {
		args = append([]string{"--id=" + deviceUUID}, args...)
	}

	_, err := m.runNvidiaSMI(args...)
	return err
}

func (m *GPUManager) CheckTargetGPUMemory(sourceGPUs []GPUDevice, targetGPUs []GPUDevice, deviceMappings []GPUDeviceMapping) error {
	if len(sourceGPUs) == 0 {
		return nil
	}

	sourceTotalMem := uint64(0)
	for _, gpu := range sourceGPUs {
		sourceTotalMem += gpu.MemoryMB
	}

	targetTotalMem := uint64(0)
	for _, gpu := range targetGPUs {
		targetTotalMem += gpu.MemoryMB
	}

	if targetTotalMem < sourceTotalMem {
		return fmt.Errorf("insufficient GPU memory on target: source needs %d MB, target has %d MB",
			sourceTotalMem, targetTotalMem)
	}

	for _, mapping := range deviceMappings {
		var sourceGPU, targetGPU *GPUDevice

		for i := range sourceGPUs {
			if sourceGPUs[i].UUID == mapping.SourceDeviceUUID {
				sourceGPU = &sourceGPUs[i]
				break
			}
		}

		for i := range targetGPUs {
			if targetGPUs[i].UUID == mapping.TargetDeviceUUID {
				targetGPU = &targetGPUs[i]
				break
			}
		}

		if sourceGPU != nil && targetGPU != nil && targetGPU.MemoryMB < sourceGPU.MemoryMB {
			return fmt.Errorf("target GPU %s (%d MB) has less memory than source GPU %s (%d MB)",
				mapping.TargetDeviceUUID, targetGPU.MemoryMB,
				mapping.SourceDeviceUUID, sourceGPU.MemoryMB)
		}
	}

	return nil
}

func isDescendantOf(parentPID int, childPID int) bool {
	if parentPID == childPID {
		return true
	}

	for childPID > 1 {
		statusPath := fmt.Sprintf("/proc/%d/status", childPID)
		data, err := os.ReadFile(statusPath)
		if err != nil {
			return false
		}

		ppid := 0
		for _, line := range strings.Split(string(data), "\n") {
			if strings.HasPrefix(line, "PPid:") {
				fields := strings.Fields(line)
				if len(fields) >= 2 {
					ppid, _ = strconv.Atoi(fields[1])
				}
				break
			}
		}

		if ppid == parentPID {
			return true
		}
		if ppid == childPID || ppid == 0 {
			return false
		}
		childPID = ppid
	}

	return false
}

func waitForGPURelease(pid int, timeout time.Duration) error {
	start := time.Now()
	for time.Since(start) < timeout {
		_, err := os.Stat(fmt.Sprintf("/proc/%d", pid))
		if err != nil {
			return fmt.Errorf("process %d exited unexpectedly", pid)
		}

		if !isProcessUsingGPU(pid) {
			return nil
		}

		time.Sleep(100 * time.Millisecond)
	}

	return fmt.Errorf("timeout waiting for GPU release after %v", timeout)
}

func isProcessUsingGPU(pid int) bool {
	cmd := exec.Command("nvidia-smi", "--query-compute-apps=pid", "--format=csv,noheader")
	output, err := cmd.Output()
	if err != nil {
		return false
	}

	pidStr := fmt.Sprintf("%d", pid)
	for _, line := range strings.Split(string(output), "\n") {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, pidStr) {
			return true
		}
	}

	return false
}

func getMIGMode(device GPUDevice) string {
	if device.MIGEnabled {
		return "Enabled"
	}
	return "Disabled"
}
