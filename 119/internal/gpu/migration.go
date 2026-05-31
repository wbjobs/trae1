//go:build linux

package gpu

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"golang.org/x/sys/unix"
)

type GPUMigrationPhase string

const (
	PhaseGPUDetect      GPUMigrationPhase = "detect"
	PhaseGPUSignal      GPUMigrationPhase = "signal"
	PhaseGPUSave        GPUMigrationPhase = "save"
	PhaseGPURestore     GPUMigrationPhase = "restore"
	PhaseGPUVerify      GPUMigrationPhase = "verify"
)

type GPUMigrationCoordinator struct {
	gpuMgr      *GPUManager
	migMgr      *MIGManager
	cudaMgr     *CUDAMemoryManager
	verbose     bool
	deviceMaps  []GPUDeviceMapping
	gpuState    *GPUState
	workDir     string
	signalTimeout time.Duration
}

func NewGPUMigrationCoordinator(verbose bool, workDir string) *GPUMigrationCoordinator {
	gpuMgr := NewGPUManager(verbose)
	return &GPUMigrationCoordinator{
		gpuMgr:      gpuMgr,
		migMgr:      NewMIGManager(gpuMgr),
		verbose:     verbose,
		workDir:     workDir,
		signalTimeout: 30 * time.Second,
	}
}

func (c *GPUMigrationCoordinator) ParseDeviceMappings(mappingStr string) error {
	if mappingStr == "" {
		return nil
	}

	mappings := make([]GPUDeviceMapping, 0)
	parts := strings.Split(mappingStr, ",")

	for _, part := range parts {
		part = strings.TrimSpace(part)
		if part == "" {
			continue
		}

		devices := strings.Split(part, "->")
		if len(devices) != 2 {
			return fmt.Errorf("invalid GPU mapping format: %s (expected source->target)", part)
		}

		source := strings.TrimSpace(devices[0])
		target := strings.TrimSpace(devices[1])

		mapping := GPUDeviceMapping{}

		if strings.HasPrefix(source, "GPU-") || strings.HasPrefix(source, "MIG-") {
			mapping.SourceDeviceUUID = source
		} else {
			fmt.Sscanf(source, "%d", &mapping.SourceDeviceID)
		}

		if strings.HasPrefix(target, "GPU-") || strings.HasPrefix(target, "MIG-") {
			mapping.TargetDeviceUUID = target
		} else {
			fmt.Sscanf(target, "%d", &mapping.TargetDeviceID)
		}

		mapping.IsMIG = strings.HasPrefix(source, "MIG-") || strings.HasPrefix(target, "MIG-")
		mappings = append(mappings, mapping)
	}

	c.deviceMaps = mappings
	return nil
}

func (c *GPUMigrationCoordinator) PreMigrationCheck(containerPID int, targetHasGPU bool) error {
	if !c.gpuMgr.IsAvailable() {
		if targetHasGPU {
			return fmt.Errorf("source host has no NVIDIA GPU but target does")
		}
		return nil
	}

	sourceGPUs, err := c.gpuMgr.GetGPUDevices()
	if err != nil {
		return fmt.Errorf("get source GPUs: %w", err)
	}

	if len(sourceGPUs) == 0 {
		return nil
	}

	if !targetHasGPU {
		return fmt.Errorf("target host has no NVIDIA GPU, but source uses GPU(s): %v", sourceGPUs)
	}

	gpuProcs, err := c.gpuMgr.GetGPUProcesses(containerPID)
	if err != nil {
		return fmt.Errorf("detect GPU processes: %w", err)
	}

	if c.verbose {
		fmt.Printf("[gpu] detected %d GPU devices and %d GPU processes in container\n",
			len(sourceGPUs), len(gpuProcs))
	}

	c.gpuState = &GPUState{
		Devices:       sourceGPUs,
		Processes:     gpuProcs,
		DeviceMappings: c.deviceMaps,
		SignalTimeout: c.signalTimeout,
	}

	for _, proc := range gpuProcs {
		fmt.Printf("[gpu] GPU process detected: PID=%d Name=%s GPU=%s Memory=%d MB\n",
			proc.PID, proc.Name, proc.GPUUUID, proc.MemoryUsedMB)
	}

	return nil
}

func (c *GPUMigrationCoordinator) ReleaseGPUResources(containerPID int) error {
	if c.gpuState == nil || len(c.gpuState.Processes) == 0 {
		return nil
	}

	fmt.Println("[gpu] releasing GPU resources before checkpoint...")

	for _, proc := range c.gpuState.Processes {
		if c.verbose {
			fmt.Printf("[gpu] sending SIGUSR1 to PID %d (%s) to release GPU\n", proc.PID, proc.Name)
		}

		if err := unix.Kill(proc.PID, unix.SIGUSR1); err != nil {
			fmt.Fprintf(os.Stderr, "[gpu] warning: failed to send SIGUSR1 to %d: %v\n", proc.PID, err)
			continue
		}

		if err := c.waitForProcessRelease(proc.PID); err != nil {
			fmt.Fprintf(os.Stderr, "[gpu] warning: timeout waiting for %d to release GPU: %v\n", proc.PID, err)
		}
	}

	time.Sleep(500 * time.Millisecond)

	remaining, err := c.gpuMgr.GetGPUProcesses(containerPID)
	if err == nil && len(remaining) > 0 {
		fmt.Fprintf(os.Stderr, "[gpu] warning: %d processes still using GPU after signal\n", len(remaining))
		for _, proc := range remaining {
			fmt.Fprintf(os.Stderr, "[gpu]   - PID=%d Name=%s\n", proc.PID, proc.Name)
		}
	}

	return nil
}

func (c *GPUMigrationCoordinator) waitForProcessRelease(pid int) error {
	start := time.Now()
	for time.Since(start) < c.signalTimeout {
		if !isProcessUsingGPU(pid) {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("timeout after %v", c.signalTimeout)
}

func (c *GPUMigrationCoordinator) SaveGPUState(checkpointDir string) error {
	if c.gpuState == nil {
		return nil
	}

	if len(c.gpuState.MemoryBuffers) > 0 {
		fmt.Printf("[gpu] saving %d GPU memory buffers...\n", len(c.gpuState.MemoryBuffers))

		for i, buf := range c.gpuState.MemoryBuffers {
			if c.cudaMgr == nil {
				var err error
				c.cudaMgr, err = NewCUDAMemoryManager(0, c.verbose)
				if err != nil {
					return fmt.Errorf("init CUDA manager: %w", err)
				}
			}

			saved, err := c.cudaMgr.SaveGPUState(buf.SourceAddr, buf.SizeBytes)
			if err != nil {
				fmt.Fprintf(os.Stderr, "[gpu] warning: failed to save buffer %d: %v\n", i, err)
				continue
			}

			c.gpuState.MemoryBuffers[i].HostBuffer = saved.HostBuffer
		}
	}

	stateFile := filepath.Join(checkpointDir, "gpu-state.json")
	data, err := json.MarshalIndent(c.gpuState, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal GPU state: %w", err)
	}

	if err := os.WriteFile(stateFile, data, 0644); err != nil {
		return fmt.Errorf("write GPU state: %w", err)
	}

	fmt.Printf("[gpu] GPU state saved to %s\n", stateFile)
	return nil
}

func (c *GPUMigrationCoordinator) LoadGPUState(checkpointDir string) error {
	stateFile := filepath.Join(checkpointDir, "gpu-state.json")
	data, err := os.ReadFile(stateFile)
	if err != nil {
		if os.IsNotExist(err) {
			return nil
		}
		return fmt.Errorf("read GPU state: %w", err)
	}

	var state GPUState
	if err := json.Unmarshal(data, &state); err != nil {
		return fmt.Errorf("unmarshal GPU state: %w", err)
	}

	c.gpuState = &state
	fmt.Printf("[gpu] loaded GPU state: %d devices, %d processes, %d memory buffers\n",
		len(state.Devices), len(state.Processes), len(state.MemoryBuffers))

	return nil
}

func (c *GPUMigrationCoordinator) RestoreGPUResources(restoredPID int, newGPUDevices []GPUDevice) error {
	if c.gpuState == nil || len(c.gpuState.Processes) == 0 {
		return nil
	}

	fmt.Println("[gpu] restoring GPU resources after checkpoint...")

	if len(c.gpuState.MemoryBuffers) > 0 && c.gpuState.DeviceMappings != nil {
		for i, mapping := range c.gpuState.DeviceMappings {
			targetDeviceID := mapping.TargetDeviceID
			if c.cudaMgr == nil {
				var err error
				c.cudaMgr, err = NewCUDAMemoryManager(targetDeviceID, c.verbose)
				if err != nil {
					fmt.Fprintf(os.Stderr, "[gpu] warning: CUDA init failed for device %d: %v\n", targetDeviceID, err)
					continue
				}
			}

			if i < len(c.gpuState.MemoryBuffers) {
				buf := &c.gpuState.MemoryBuffers[i]
				if len(buf.HostBuffer) > 0 {
					err := c.cudaMgr.RestoreGPUState(buf, buf.SourceAddr)
					if err != nil {
						fmt.Fprintf(os.Stderr, "[gpu] warning: restore buffer %d failed: %v\n", i, err)
					}
				}
			}
		}
	}

	for _, proc := range c.gpuState.Processes {
		newPID := findRestoredPID(proc.PID, restoredPID)
		if newPID <= 0 {
			newPID = proc.PID
		}

		if c.verbose {
			fmt.Printf("[gpu] sending SIGUSR2 to PID %d to restore GPU\n", newPID)
		}

		if err := unix.Kill(newPID, unix.SIGUSR2); err != nil {
			fmt.Fprintf(os.Stderr, "[gpu] warning: failed to send SIGUSR2 to %d: %v\n", newPID, err)
		}
	}

	time.Sleep(1 * time.Second)
	return nil
}

func (c *GPUMigrationCoordinator) VerifyGPU(targetGPUs []GPUDevice) error {
	if c.gpuState == nil || len(c.gpuState.Devices) == 0 {
		return nil
	}

	fmt.Println("[gpu] verifying GPU availability on target...")

	if err := c.gpuMgr.CheckTargetGPUMemory(c.gpuState.Devices, targetGPUs, c.deviceMaps); err != nil {
		return fmt.Errorf("target GPU check failed: %w", err)
	}

	if err := c.gpuMgr.TestGPUAvailability(""); err != nil {
		return fmt.Errorf("GPU test failed: %w", err)
	}

	for _, mapping := range c.deviceMaps {
		var found bool
		for _, gpu := range targetGPUs {
			if gpu.UUID == mapping.TargetDeviceUUID || gpu.ID == mapping.TargetDeviceID {
				found = true
				break
			}
		}
		if !found && mapping.TargetDeviceUUID != "" {
			return fmt.Errorf("target GPU %s not found", mapping.TargetDeviceUUID)
		}
	}

	fmt.Println("[gpu] target GPU verification passed")
	return nil
}

func (c *GPUMigrationCoordinator) Cleanup() {
	if c.cudaMgr != nil {
		c.cudaMgr.Cleanup()
	}
}

func (c *GPUMigrationCoordinator) GetState() *GPUState {
	return c.gpuState
}

func (c *GPUMigrationCoordinator) GetGPUDevices() []GPUDevice {
	if !c.gpuMgr.IsAvailable() {
		return nil
	}
	devices, _ := c.gpuMgr.GetGPUDevices()
	return devices
}

func (c *GPUMigrationCoordinator) SetSignalTimeout(timeout time.Duration) {
	c.signalTimeout = timeout
}

func findRestoredPID(oldPID int, basePID int) int {
	if oldPID == basePID {
		return basePID
	}

	children := getChildProcesses(basePID)
	if len(children) == 0 {
		return oldPID
	}

	return oldPID
}

func getChildProcesses(parentPID int) []int {
	children := make([]int, 0)
	entries, err := os.ReadDir("/proc")
	if err != nil {
		return children
	}

	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}

		pid := 0
		fmt.Sscanf(entry.Name(), "%d", &pid)
		if pid == 0 {
			continue
		}

		data, err := os.ReadFile(fmt.Sprintf("/proc/%d/status", pid))
		if err != nil {
			continue
		}

		for _, line := range strings.Split(string(data), "\n") {
			if strings.HasPrefix(line, "PPid:") {
				ppid := 0
				fmt.Sscanf(line, "PPid:\t%d", &ppid)
				if ppid == parentPID {
					children = append(children, pid)
				}
				break
			}
		}
	}

	return children
}

func (c *GPUMigrationCoordinator) AddMemoryBuffer(sourceAddr uint64, sizeBytes uint64, deviceUUID string) {
	if c.gpuState == nil {
		c.gpuState = &GPUState{}
	}

	c.gpuState.MemoryBuffers = append(c.gpuState.MemoryBuffers, GPUMemoryBuffer{
		SourceAddr: sourceAddr,
		SizeBytes:  sizeBytes,
		DeviceUUID: deviceUUID,
	})
}

func (c *GPUMigrationCoordinator) ListGPUs() ([]GPUDevice, error) {
	return c.gpuMgr.GetGPUDevices()
}

func (c *GPUMigrationCoordinator) ListMIGDevices() ([]MIGDevice, error) {
	return c.gpuMgr.GetMIGDevices()
}
