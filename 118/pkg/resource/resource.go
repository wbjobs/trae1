package resource

import (
	"encoding/json"
	"fmt"
	"net"
	"os/exec"
	"strings"
	"time"

	"github.com/lxc-migrate/lxc-migrate/pkg/gpu"
	"github.com/lxc-migrate/lxc-migrate/pkg/lxc"
)

type HostResources struct {
	CPUCores    int     `json:"cpu_cores"`
	TotalMemory int64   `json:"total_memory"`
	FreeMemory  int64   `json:"free_memory"`
	TotalDisk   int64   `json:"total_disk"`
	FreeDisk    int64   `json:"free_disk"`
	LoadAvg     float64 `json:"load_avg"`
	LXCSupport  bool    `json:"lxc_support"`
	CRIUSupport bool    `json:"criu_support"`
	HasGPU      bool    `json:"has_gpu"`
	GPUs        []gpu.GPUInfo `json:"gpus,omitempty"`
}

type ResourceRequest struct {
	Memory     int64
	CPUCores   int
	DiskSpace  int64
}

func CheckTargetResources(host string, port int) (*HostResources, error) {
	addr := fmt.Sprintf("%s:%d", host, port)
	conn, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		return nil, fmt.Errorf("无法连接目标主机: %w", err)
	}
	defer conn.Close()

	req := map[string]string{"action": "check_resources"}
	data, _ := json.Marshal(req)
	if _, err := conn.Write(data); err != nil {
		return nil, fmt.Errorf("发送请求失败: %w", err)
	}

	buf := make([]byte, 4096)
	n, err := conn.Read(buf)
	if err != nil {
		return nil, fmt.Errorf("接收响应失败: %w", err)
	}

	var resources HostResources
	if err := json.Unmarshal(buf[:n], &resources); err != nil {
		return nil, fmt.Errorf("解析响应失败: %w", err)
	}

	return &resources, nil
}

func CheckLocalResources() (*HostResources, error) {
	resources := &HostResources{
		LXCSupport:  true,
		CRIUSupport: true,
	}

	cores, err := getCPUCores()
	if err == nil {
		resources.CPUCores = cores
	}

	totalMem, freeMem, err := getMemoryInfo()
	if err == nil {
		resources.TotalMemory = totalMem
		resources.FreeMemory = freeMem
	}

	totalDisk, freeDisk, err := getDiskInfo()
	if err == nil {
		resources.TotalDisk = totalDisk
		resources.FreeDisk = freeDisk
	}

	loadAvg, err := getLoadAvg()
	if err == nil {
		resources.LoadAvg = loadAvg
	}

	gpuState, err := gpu.DetectGPU()
	if err == nil && gpuState.HasGPU {
		resources.HasGPU = true
		resources.GPUs = gpuState.GPUs
	}

	return resources, nil
}

func CheckGPUCompatibility(sourceGPUs, targetGPUs *gpu.GPUState, mappings []gpu.GPUDeviceMapping) error {
	return gpu.ValidateGPUMapping(sourceGPUs, targetGPUs, mappings)
}

func ValidateGPUResources(sourceGPUState, targetGPUState *gpu.GPUState, mappings []gpu.GPUDeviceMapping) error {
	if !sourceGPUState.HasGPU {
		return nil
	}

	if !targetGPUState.HasGPU {
		return fmt.Errorf("源主机有GPU但目标主机无GPU，迁移取消。使用 --force 可强制迁移（不推荐）")
	}

	return gpu.ValidateGPUMapping(sourceGPUState, targetGPUState, mappings)
}

func ValidateResources(containerInfo *lxc.ContainerInfo, hostResources *HostResources) error {
	if !hostResources.LXCSupport {
		return fmt.Errorf("目标主机不支持LXC")
	}
	if !hostResources.CRIUSupport {
		return fmt.Errorf("目标主机不支持CRIU")
	}

	requiredMemory := containerInfo.MemoryUsage * 12 / 10
	if hostResources.FreeMemory < requiredMemory {
		return fmt.Errorf("目标主机内存不足: 需要 %d, 可用 %d",
			requiredMemory, hostResources.FreeMemory)
	}

	requiredDisk := containerInfo.MemoryUsage * 2
	if hostResources.FreeDisk < requiredDisk {
		return fmt.Errorf("目标主机磁盘空间不足: 需要 %d, 可用 %d",
			requiredDisk, hostResources.FreeDisk)
	}

	if hostResources.LoadAvg > float64(hostResources.CPUCores)*2 {
		return fmt.Errorf("目标主机负载过高: %.2f", hostResources.LoadAvg)
	}

	return nil
}

func CheckContainerHealth(host string, port int, containerName string) (bool, error) {
	addr := fmt.Sprintf("%s:%d", host, port)
	conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
	if err != nil {
		return false, err
	}
	defer conn.Close()

	req := map[string]string{
		"action":    "check_health",
		"container": containerName,
	}
	data, _ := json.Marshal(req)
	if _, err := conn.Write(data); err != nil {
		return false, err
	}

	buf := make([]byte, 1024)
	n, err := conn.Read(buf)
	if err != nil {
		return false, err
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(buf[:n], &resp); err != nil {
		return false, err
	}

	healthy, ok := resp["healthy"].(bool)
	return ok && healthy, nil
}

func getCPUCores() (int, error) {
	cmd := exec.Command("nproc")
	output, err := cmd.Output()
	if err != nil {
		return 0, err
	}

	var cores int
	fmt.Sscanf(strings.TrimSpace(string(output)), "%d", &cores)
	return cores, nil
}

func getMemoryInfo() (total, free int64, err error) {
	cmd := exec.Command("free", "-b")
	output, err := cmd.Output()
	if err != nil {
		return 0, 0, err
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.HasPrefix(line, "Mem:") {
			fields := strings.Fields(line)
			if len(fields) >= 4 {
				fmt.Sscanf(fields[1], "%d", &total)
				fmt.Sscanf(fields[3], "%d", &free)
			}
		}
	}
	return total, free, nil
}

func getDiskInfo() (total, free int64, err error) {
	cmd := exec.Command("df", "-B1", "/")
	output, err := cmd.Output()
	if err != nil {
		return 0, 0, err
	}

	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		if strings.Contains(line, "/") {
			fields := strings.Fields(line)
			if len(fields) >= 4 {
				fmt.Sscanf(fields[1], "%d", &total)
				fmt.Sscanf(fields[3], "%d", &free)
			}
		}
	}
	return total, free, nil
}

func getLoadAvg() (float64, error) {
	cmd := exec.Command("cat", "/proc/loadavg")
	output, err := cmd.Output()
	if err != nil {
		return 0, err
	}

	var load1, load5, load15 float64
	fmt.Sscanf(string(output), "%f %f %f", &load1, &load5, &load15)
	return load1, nil
}
