//go:build !linux

package gpu

import (
	"fmt"
	"time"
)

type GPUMigrationPhase string

const (
	PhaseGPUDetect  GPUMigrationPhase = "detect"
	PhaseGPUSignal  GPUMigrationPhase = "signal"
	PhaseGPUSave    GPUMigrationPhase = "save"
	PhaseGPURestore GPUMigrationPhase = "restore"
	PhaseGPUVerify  GPUMigrationPhase = "verify"
)

type GPUMigrationCoordinator struct {
	verbose     bool
	deviceMaps  []GPUDeviceMapping
	gpuState    *GPUState
	workDir     string
	signalTimeout time.Duration
}

func NewGPUMigrationCoordinator(verbose bool, workDir string) *GPUMigrationCoordinator {
	return &GPUMigrationCoordinator{
		verbose: verbose,
		workDir: workDir,
		signalTimeout: 30 * time.Second,
	}
}

func (c *GPUMigrationCoordinator) ParseDeviceMappings(mappingStr string) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) PreMigrationCheck(containerPID int, targetHasGPU bool) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) ReleaseGPUResources(containerPID int) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) SaveGPUState(checkpointDir string) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) LoadGPUState(checkpointDir string) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) RestoreGPUResources(restoredPID int, newGPUDevices []GPUDevice) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) VerifyGPU(targetGPUs []GPUDevice) error {
	return fmt.Errorf("GPU migration only available on Linux")
}

func (c *GPUMigrationCoordinator) Cleanup() {}

func (c *GPUMigrationCoordinator) GetState() *GPUState {
	return c.gpuState
}

func (c *GPUMigrationCoordinator) GetGPUDevices() []GPUDevice {
	return nil
}

func (c *GPUMigrationCoordinator) SetSignalTimeout(timeout time.Duration) {
	c.signalTimeout = timeout
}

func (c *GPUMigrationCoordinator) AddMemoryBuffer(sourceAddr uint64, sizeBytes uint64, deviceUUID string) {}

func (c *GPUMigrationCoordinator) ListGPUs() ([]GPUDevice, error) {
	return nil, fmt.Errorf("GPU management only available on Linux")
}

func (c *GPUMigrationCoordinator) ListMIGDevices() ([]MIGDevice, error) {
	return nil, fmt.Errorf("GPU management only available on Linux")
}
