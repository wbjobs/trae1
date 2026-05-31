//go:build !linux

package gpu

import "fmt"

type MIGConfig struct {
	GPUDeviceID     int      `json:"gpu_device_id"`
	Enabled         bool     `json:"enabled"`
	Mode            string   `json:"mode"`
	GIInstances     []MIGGI  `json:"gi_instances"`
	CIInstances     []MIGCI  `json:"ci_instances"`
}

type MIGGI struct {
	ID       int      `json:"id"`
	Profile  string   `json:"profile"`
	CIIDs    []int    `json:"ci_ids"`
	MemoryMB uint64   `json:"memory_mb"`
}

type MIGCI struct {
	ID       int      `json:"id"`
	GIID     int      `json:"gi_id"`
	Profile  string   `json:"profile"`
	MemoryMB uint64   `json:"memory_mb"`
	UUID     string   `json:"uuid"`
}

type MIGManager struct {
	gpuMgr *GPUManager
}

func NewMIGManager(gpuMgr *GPUManager) *MIGManager {
	return &MIGManager{
		gpuMgr: gpuMgr,
	}
}

func (m *MIGManager) IsMIGSupported(gpuID int) (bool, error) {
	return false, fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) GetMIGConfig(gpuID int) (*MIGConfig, error) {
	return nil, fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) EnableMIG(gpuID int) error {
	return fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) DisableMIG(gpuID int) error {
	return fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) CreateMIGPartition(gpuID int, profile string) (string, error) {
	return "", fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) DestroyMIGPartition(gpuID int, giID int) error {
	return fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) ValidateMIGMapping(sourceMIG *MIGConfig, targetMIG *MIGConfig) error {
	return fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) GetDeviceUUIDByCIID(gpuID int, giID int, ciID int) (string, error) {
	return "", fmt.Errorf("MIG management only available on Linux")
}

func (m *MIGManager) ListMIGProfiles() ([]string, error) {
	return nil, fmt.Errorf("MIG management only available on Linux")
}
