//go:build linux

package gpu

import (
	"fmt"
	"os/exec"
	"strconv"
	"strings"
)

type MIGConfig struct {
	GPUDeviceID     int          `json:"gpu_device_id"`
	Enabled         bool         `json:"enabled"`
	Mode            string       `json:"mode"`
	GIInstances     []MIGGI      `json:"gi_instances"`
	CIInstances     []MIGCI      `json:"ci_instances"`
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
	if !m.gpuMgr.IsAvailable() {
		return false, fmt.Errorf("no NVIDIA GPU available")
	}

	output, err := m.gpuMgr.runNvidiaSMI(fmt.Sprintf("--id=%d", gpuID), "--query-gpu=mig_mode.current", "--format=csv,noheader")
	if err != nil {
		return false, err
	}

	return strings.Contains(output, "Enabled") || strings.Contains(output, "1"), nil
}

func (m *MIGManager) GetMIGConfig(gpuID int) (*MIGConfig, error) {
	supported, err := m.IsMIGSupported(gpuID)
	if err != nil {
		return nil, err
	}

	config := &MIGConfig{
		GPUDeviceID: gpuID,
		Enabled:     supported,
		Mode:        "Disabled",
	}

	if !supported {
		return config, nil
	}

	config.Mode = "Enabled"

	output, err := m.gpuMgr.runNvidiaSMI(fmt.Sprintf("--id=%d", gpuID),
		"--query-gpu-instances=instance_id,profile,instance_type,memory",
		"--format=csv,noheader")
	if err != nil {
		return config, nil
	}

	for _, line := range strings.Split(strings.TrimSpace(output), "\n") {
		fields := strings.Split(line, ", ")
		if len(fields) >= 4 {
			id, _ := strconv.Atoi(fields[0])
			mem, _ := parseMemoryMB(fields[3])
			config.GIInstances = append(config.GIInstances, MIGGI{
				ID:       id,
				Profile:  fields[1],
				MemoryMB: mem,
			})
		}
	}

	output, err = m.gpuMgr.runNvidiaSMI(fmt.Sprintf("--id=%d", gpuID),
		"--query-compute-instances=instance_id,gi_instance_id,profile,memory,uuid",
		"--format=csv,noheader")
	if err != nil {
		return config, nil
	}

	for _, line := range strings.Split(strings.TrimSpace(output), "\n") {
		fields := strings.Split(line, ", ")
		if len(fields) >= 5 {
			id, _ := strconv.Atoi(fields[0])
			giID, _ := strconv.Atoi(fields[1])
			mem, _ := parseMemoryMB(fields[3])

			config.CIInstances = append(config.CIInstances, MIGCI{
				ID:       id,
				GIID:     giID,
				Profile:  fields[2],
				MemoryMB: mem,
				UUID:     fields[4],
			})

			for i := range config.GIInstances {
				if config.GIInstances[i].ID == giID {
					config.GIInstances[i].CIIDs = append(config.GIInstances[i].CIIDs, id)
				}
			}
		}
	}

	return config, nil
}

func (m *MIGManager) EnableMIG(gpuID int) error {
	cmd := exec.Command("nvidia-smi", fmt.Sprintf("--id=%d", gpuID), "--mig-mode=1")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("enable MIG mode: %w: %s", err, string(output))
	}
	return nil
}

func (m *MIGManager) DisableMIG(gpuID int) error {
	cmd := exec.Command("nvidia-smi", fmt.Sprintf("--id=%d", gpuID), "--mig-mode=0")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("disable MIG mode: %w: %s", err, string(output))
	}
	return nil
}

func (m *MIGManager) CreateMIGPartition(gpuID int, profile string) (string, error) {
	cmd := exec.Command("nvidia-smi", fmt.Sprintf("--id=%d", gpuID),
		"--create-gpu-instance", fmt.Sprintf("--gpu-instance-profile=%s", profile))
	output, err := cmd.CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("create MIG partition: %w: %s", err, string(output))
	}

	uuid := ""
	for _, line := range strings.Split(string(output), "\n") {
		if strings.Contains(line, "GPU instance UUID") {
			parts := strings.Split(line, ":")
			if len(parts) >= 2 {
				uuid = strings.TrimSpace(parts[1])
			}
		}
	}

	return uuid, nil
}

func (m *MIGManager) DestroyMIGPartition(gpuID int, giID int) error {
	cmd := exec.Command("nvidia-smi", fmt.Sprintf("--id=%d", gpuID),
		"--destroy-gpu-instance", fmt.Sprintf("--gpu-instance-id=%d", giID))
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("destroy MIG partition: %w: %s", err, string(output))
	}
	return nil
}

func (m *MIGManager) ValidateMIGMapping(sourceMIG *MIGConfig, targetMIG *MIGConfig) error {
	if !sourceMIG.Enabled && !targetMIG.Enabled {
		return nil
	}

	if sourceMIG.Enabled && !targetMIG.Enabled {
		return fmt.Errorf("source GPU %d has MIG enabled but target GPU %d does not",
			sourceMIG.GPUDeviceID, targetMIG.GPUDeviceID)
	}

	if len(sourceMIG.CIInstances) > len(targetMIG.CIInstances) {
		return fmt.Errorf("source has %d MIG compute instances but target only has %d",
			len(sourceMIG.CIInstances), len(targetMIG.CIInstances))
	}

	for _, sourceCI := range sourceMIG.CIInstances {
		found := false
		for _, targetCI := range targetMIG.CIInstances {
			if targetCI.MemoryMB >= sourceCI.MemoryMB && targetCI.Profile == sourceCI.Profile {
				found = true
				break
			}
		}
		if !found {
			return fmt.Errorf("no matching MIG CI for source profile %s (memory: %d MB)",
				sourceCI.Profile, sourceCI.MemoryMB)
		}
	}

	return nil
}

func (m *MIGManager) GetDeviceUUIDByCIID(gpuID int, giID int, ciID int) (string, error) {
	output, err := m.gpuMgr.runNvidiaSMI(fmt.Sprintf("--id=%d", gpuID),
		"--query-compute-instances=uuid",
		fmt.Sprintf("--gi=%d", giID),
		fmt.Sprintf("--ci=%d", ciID),
		"--format=csv,noheader")
	if err != nil {
		return "", err
	}

	return strings.TrimSpace(output), nil
}

func (m *MIGManager) ListMIGProfiles() ([]string, error) {
	profiles := make([]string, 0)

	devices, err := m.gpuMgr.GetGPUDevices()
	if err != nil {
		return nil, err
	}

	for _, dev := range devices {
		if dev.MIGEnabled {
			profiles = append(profiles, fmt.Sprintf("GPU %d: MIG enabled", dev.ID))
		}
	}

	return profiles, nil
}
