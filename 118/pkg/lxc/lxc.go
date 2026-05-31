package lxc

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

type ContainerInfo struct {
	Name        string            `json:"name"`
	PID         int               `json:"pid"`
	Status      string            `json:"status"`
	MemoryUsage int64             `json:"memory_usage"`
	CPUUsage    float64           `json:"cpu_usage"`
	Networks    []NetworkConfig   `json:"networks"`
}

type NetworkConfig struct {
	Name   string `json:"name"`
	Type   string `json:"type"`
	HWAddr string `json:"hwaddr"`
	IPv4   string `json:"ipv4"`
	IPv6   string `json:"ipv6"`
}

func CheckContainerExists(name string) error {
	cmd := exec.Command("lxc-info", "-n", name)
	output, err := cmd.CombinedOutput()
	if err != nil {
		if strings.Contains(string(output), "doesn't exist") {
			return fmt.Errorf("容器 '%s' 不存在", name)
		}
		return fmt.Errorf("检查容器失败: %w", err)
	}
	return nil
}

func CheckContainerRunning(name string) error {
	info, err := GetContainerInfo(name)
	if err != nil {
		return err
	}
	if info.Status != "RUNNING" {
		return fmt.Errorf("容器 '%s' 未运行 (状态: %s)", name, info.Status)
	}
	return nil
}

func GetContainerInfo(name string) (*ContainerInfo, error) {
	cmd := exec.Command("lxc-info", "-n", name, "-H", "-p")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("获取容器信息失败: %w", err)
	}

	info := &ContainerInfo{Name: name}
	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		parts := strings.SplitN(line, ":", 2)
		if len(parts) != 2 {
			continue
		}
		key := strings.TrimSpace(parts[0])
		value := strings.TrimSpace(parts[1])

		switch key {
		case "pid":
			info.PID, _ = strconv.Atoi(value)
		case "state":
			info.Status = value
		}
	}

	cmd = exec.Command("lxc-cgroup", "-n", name, "memory.usage_in_bytes")
	output, err = cmd.Output()
	if err == nil {
		info.MemoryUsage, _ = strconv.ParseInt(strings.TrimSpace(string(output)), 10, 64)
	}

	info.Networks, _ = getNetworkConfigs(name)

	return info, nil
}

func getNetworkConfigs(name string) ([]NetworkConfig, error) {
	cmd := exec.Command("lxc-ls", "-f", "-F", "name,network", name)
	output, err := cmd.Output()
	if err != nil {
		return nil, err
	}

	var configs []NetworkConfig
	lines := strings.Split(string(output), "\n")
	for i, line := range lines {
		if i == 0 || strings.TrimSpace(line) == "" {
			continue
		}
		parts := strings.Fields(line)
		if len(parts) >= 2 {
			configs = append(configs, NetworkConfig{
				Name: parts[0],
				IPv4: parts[1],
			})
		}
	}
	return configs, nil
}

func PrepareMigration(name string) error {
	if err := exec.Command("lxc-freeze", "-n", name).Run(); err != nil {
		return fmt.Errorf("冻结容器失败: %w", err)
	}
	return nil
}

func CreateCheckpoint(name, dir string, leaveRunning bool) error {
	if err := os.MkdirAll(dir, 0755); err != nil {
		return fmt.Errorf("创建检查点目录失败: %w", err)
	}

	args := []string{
		"checkpoint",
		"-n", name,
		"-D", dir,
		"--verbose",
	}
	if leaveRunning {
		args = append(args, "-R")
	}

	cmd := exec.Command("lxc-checkpoint", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("创建检查点失败: %w", err)
	}

	return nil
}

func RestoreContainer(name, dir string) error {
	args := []string{
		"restore",
		"-n", name,
		"-D", dir,
		"--verbose",
	}

	cmd := exec.Command("lxc-checkpoint", args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("恢复容器失败: %w", err)
	}

	return nil
}

func CleanupCheckpoint(dir string) error {
	return os.RemoveAll(dir)
}

func StopContainer(name string) error {
	cmd := exec.Command("lxc-stop", "-n", name)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("停止容器失败: %w", err)
	}
	return nil
}

func StartContainer(name string) error {
	cmd := exec.Command("lxc-start", "-n", name, "-d")
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("启动容器失败: %w", err)
	}
	return nil
}

func GetContainerConfig(name string) (map[string]string, error) {
	cmd := exec.Command("lxc-config", "-l", name)
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("获取容器配置失败: %w", err)
	}

	config := make(map[string]string)
	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		parts := strings.SplitN(line, "=", 2)
		if len(parts) == 2 {
			config[strings.TrimSpace(parts[0])] = strings.TrimSpace(parts[1])
		}
	}
	return config, nil
}

func UpdateContainerConfig(name string, updates map[string]string) error {
	configPath := fmt.Sprintf("/var/lib/lxc/%s/config", name)
	content, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("读取配置文件失败: %w", err)
	}

	lines := strings.Split(string(content), "\n")
	for i, line := range lines {
		for key, newValue := range updates {
			if strings.HasPrefix(strings.TrimSpace(line), key) {
				lines[i] = fmt.Sprintf("%s = %s", key, newValue)
				delete(updates, key)
			}
		}
	}

	for key, value := range updates {
		lines = append(lines, fmt.Sprintf("%s = %s", key, value))
	}

	return os.WriteFile(configPath, []byte(strings.Join(lines, "\n")), 0644)
}

func MarshalInfo(info *ContainerInfo) ([]byte, error) {
	return json.Marshal(info)
}
