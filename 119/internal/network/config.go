package network

import (
	"fmt"
	"os"
	"os/exec"
	"regexp"
	"strings"

	"github.com/lxc-migrate/lxc-migrate/internal/types"
)

func GetContainerIP(containerName string) (string, error) {
	infoPath := fmt.Sprintf("/var/lib/lxc/%s/config", containerName)
	data, err := os.ReadFile(infoPath)
	if err != nil {
		return "", fmt.Errorf("read container config: %w", err)
	}

	re := regexp.MustCompile(`lxc\.net\.0\.ipv4\.address\s*=\s*([0-9.]+)`)
	matches := re.FindStringSubmatch(string(data))
	if len(matches) > 1 {
		return matches[1], nil
	}

	cmd := exec.Command("lxc-info", "-n", containerName, "-iH")
	output, err := cmd.Output()
	if err == nil && len(output) > 0 {
		return strings.TrimSpace(string(output)), nil
	}

	return "", fmt.Errorf("could not determine IP for container %s", containerName)
}

func GetContainerInterface(containerName string) (string, error) {
	configPath := fmt.Sprintf("/var/lib/lxc/%s/config", containerName)
	data, err := os.ReadFile(configPath)
	if err != nil {
		return "eth0", nil
	}

	re := regexp.MustCompile(`lxc\.net\.0\.name\s*=\s*(\S+)`)
	matches := re.FindStringSubmatch(string(data))
	if len(matches) > 1 {
		return matches[1], nil
	}

	return "eth0", nil
}

func UpdateContainerConfig(containerName string, config types.NetworkConfig) error {
	configPath := fmt.Sprintf("/var/lib/lxc/%s/config", containerName)
	data, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("read container config: %w", err)
	}

	content := string(data)

	ipRe := regexp.MustCompile(`(lxc\.net\.0\.ipv4\.address\s*=\s*)[0-9.]+`)
	content = ipRe.ReplaceAllString(content, fmt.Sprintf("${1}%s", config.NewIP))

	hwRe := regexp.MustCompile(`(lxc\.net\.0\.hwaddr\s*=\s*)\S+`)
	content = hwRe.ReplaceAllString(content, "${1}"+generateMACAddress())

	if err := os.WriteFile(configPath, []byte(content), 0644); err != nil {
		return fmt.Errorf("write container config: %w", err)
	}

	return nil
}

func generateMACAddress() string {
	return fmt.Sprintf("00:16:3e:%02x:%02x:%02x",
		os.Getpid()%256,
		os.Getppid()%256,
		os.Getuid()%256,
	)
}

func ApplyNetworkConfig(containerName string, config types.NetworkConfig) error {
	if err := UpdateContainerConfig(containerName, config); err != nil {
		return fmt.Errorf("update config: %w", err)
	}

	if err := notifyContainer(containerName, config); err != nil {
		return fmt.Errorf("notify container: %w", err)
	}

	return nil
}

func notifyContainer(containerName string, config types.NetworkConfig) error {
	notifyScript := fmt.Sprintf(`
#!/bin/sh
CONTAINER="%s"
OLD_IP="%s"
NEW_IP="%s"
IFACE="%s"

cat << EOF | lxc-attach -n "$CONTAINER" -- /bin/sh
ip addr flush dev "$IFACE"
ip addr add "$NEW_IP"/24 dev "$IFACE"
ip link set "$IFACE" up
EOF
`, containerName, config.OldIP, config.NewIP, config.Interface)

	cmd := exec.Command("sh", "-c", notifyScript)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to reconfigure network in container: %w", err)
	}

	return nil
}

func GetGateway() (string, error) {
	cmd := exec.Command("ip", "route", "show", "default")
	output, err := cmd.Output()
	if err != nil {
		return "", err
	}

	re := regexp.MustCompile(`via\s+([0-9.]+)`)
	matches := re.FindStringSubmatch(string(output))
	if len(matches) > 1 {
		return matches[1], nil
	}

	return "", fmt.Errorf("could not determine default gateway")
}

func SetIPAddress(containerName string, newIP string, iface string) error {
	script := fmt.Sprintf(`
ip addr flush dev %s
ip addr add %s/24 dev %s
ip link set %s up
`, iface, newIP, iface, iface)

	cmd := exec.Command("lxc-attach", "-n", containerName, "--", "/bin/sh", "-c", script)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}
