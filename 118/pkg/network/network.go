package network

import (
	"encoding/json"
	"fmt"
	"net"
	"os/exec"
	"strings"
	"time"
)

type NetworkUpdateRequest struct {
	Action        string `json:"action"`
	ContainerName string `json:"container_name"`
	NewIP         string `json:"new_ip"`
	OldIP         string `json:"old_ip"`
}

type NetworkConfig struct {
	Interface string
	IPAddr    string
	Netmask   string
	Gateway   string
}

func UpdateNetworkConfig(host string, port int, containerName string) error {
	addr := fmt.Sprintf("%s:%d", host, port)
	conn, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		return fmt.Errorf("连接目标主机失败: %w", err)
	}
	defer conn.Close()

	req := NetworkUpdateRequest{
		Action:        "update_network",
		ContainerName: containerName,
	}

	data, _ := json.Marshal(req)
	if _, err := conn.Write(data); err != nil {
		return fmt.Errorf("发送网络更新请求失败: %w", err)
	}

	buf := make([]byte, 4096)
	n, err := conn.Read(buf)
	if err != nil {
		return fmt.Errorf("接收网络更新响应失败: %w", err)
	}

	var resp map[string]interface{}
	if err := json.Unmarshal(buf[:n], &resp); err != nil {
		return fmt.Errorf("解析响应失败: %w", err)
	}

	if success, ok := resp["success"].(bool); ok && success {
		return nil
	}

	return fmt.Errorf("网络配置更新失败: %v", resp["error"])
}

func GetContainerNetworkConfig(containerName string) ([]NetworkConfig, error) {
	cmd := exec.Command("lxc-info", "-n", containerName, "-H", "-i")
	output, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("获取网络配置失败: %w", err)
	}

	var configs []NetworkConfig
	lines := strings.Split(string(output), "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}

		parts := strings.Fields(line)
		if len(parts) >= 2 {
			config := NetworkConfig{
				Interface: parts[0],
				IPAddr:    parts[1],
			}
			if len(parts) >= 3 {
				config.Netmask = parts[2]
			}
			configs = append(configs, config)
		}
	}

	return configs, nil
}

func ConfigureContainerNetwork(containerName string, configs []NetworkConfig) error {
	for _, config := range configs {
		ipCmd := fmt.Sprintf("lxc-attach -n %s -- ip addr add %s/%s dev %s",
			containerName, config.IPAddr, config.Netmask, config.Interface)

		cmd := exec.Command("bash", "-c", ipCmd)
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("配置网络接口 %s 失败: %w", config.Interface, err)
		}

		if config.Gateway != "" {
			gwCmd := fmt.Sprintf("lxc-attach -n %s -- ip route add default via %s",
				containerName, config.Gateway)
			cmd = exec.Command("bash", "-c", gwCmd)
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("配置默认网关失败: %w", err)
			}
		}
	}

	return nil
}

func NotifyContainerApplications(containerName string, oldIP, newIP string) error {
	notifyScript := fmt.Sprintf(`
cat > /tmp/network_notify.sh << 'EOF'
#!/bin/bash
OLD_IP=%s
NEW_IP=%s

echo "网络变更通知: $OLD_IP -> $NEW_IP"

for pid in /proc/[0-9]*; do
    pid_num=$(basename $pid)
    if [ -f "$pid/cmdline" ]; then
        cmdline=$(tr '\0' ' ' < "$pid/cmdline" 2>/dev/null)
        if echo "$cmdline" | grep -q "nginx\|apache\|mysql\|redis\|postgres"; then
            echo "通知进程: $cmdline (PID: $pid_num)"
            kill -HUP $pid_num 2>/dev/null || true
        fi
    fi
done

if command -v systemctl &> /dev/null; then
    systemctl daemon-reload 2>/dev/null || true
fi

echo "网络通知完成"
EOF
chmod +x /tmp/network_notify.sh
`, oldIP, newIP)

	cmd := exec.Command("lxc-attach", "-n", containerName, "--", "bash", "-c", notifyScript)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("通知容器内应用失败: %w", err)
	}

	return nil
}

func UpdateHostsFile(containerName string, oldIP, newIP string) error {
	updateScript := fmt.Sprintf(`
OLD_IP="%s"
NEW_IP="%s"
if [ -f /etc/hosts ]; then
    sed -i "s/$OLD_IP/$NEW_IP/g" /etc/hosts
fi
`, oldIP, newIP)

	cmd := exec.Command("lxc-attach", "-n", containerName, "--", "bash", "-c", updateScript)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("更新hosts文件失败: %w", err)
	}

	return nil
}

func RefreshNetworkServices(containerName string) error {
	refreshScript := `
if command -v systemctl &> /dev/null; then
    systemctl restart network 2>/dev/null || \
    systemctl restart NetworkManager 2>/dev/null || \
    systemctl restart systemd-networkd 2>/dev/null || true
elif command -v service &> /dev/null; then
    service network restart 2>/dev/null || \
    service networking restart 2>/dev/null || true
fi

ip addr flush dev eth0 2>/dev/null || true
dhclient eth0 2>/dev/null || true
`

	cmd := exec.Command("lxc-attach", "-n", containerName, "--", "bash", "-c", refreshScript)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("刷新网络服务失败: %w", err)
	}

	return nil
}

func CheckNetworkConnectivity(containerName string) error {
	checkScript := `
ping -c 1 -W 2 8.8.8.8 &> /dev/null && echo "NETWORK_OK" || echo "NETWORK_FAIL"
`

	cmd := exec.Command("lxc-attach", "-n", containerName, "--", "bash", "-c", checkScript)
	output, err := cmd.Output()
	if err != nil {
		return fmt.Errorf("网络连通性检查失败: %w", err)
	}

	if !strings.Contains(string(output), "NETWORK_OK") {
		return fmt.Errorf("容器网络连通性检查失败")
	}

	return nil
}
