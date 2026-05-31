package lxc

import (
	"fmt"
	"os/exec"
	"strings"
)

type CRIUConfig struct {
	LogFile     string
	LogLevel    int
	ShellJob    bool
	ExtUnixSk   bool
	TcpEstablished bool
	FileLocks   bool
	EvasiveDevices bool
	LinkRemap   bool
}

func DefaultCRIUConfig() *CRIUConfig {
	return &CRIUConfig{
		LogFile:        "criu.log",
		LogLevel:       2,
		ShellJob:       true,
		ExtUnixSk:      true,
		TcpEstablished: true,
		FileLocks:      true,
		EvasiveDevices: false,
		LinkRemap:      false,
	}
}

func CheckCRIUAvailable() error {
	cmd := exec.Command("criu", "--version")
	output, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("CRIU未安装或不可用: %w", err)
	}
	if !strings.Contains(string(output), "Version") {
		return fmt.Errorf("CRIU版本信息异常")
	}
	return nil
}

func CheckCRIUKernelSupport() error {
	requiredConfigs := []string{
		"CONFIG_CHECKPOINT_RESTORE",
		"CONFIG_NAMESPACES",
		"CONFIG_UTS_NS",
		"CONFIG_IPC_NS",
		"CONFIG_PID_NS",
		"CONFIG_NET_NS",
	}

	for _, config := range requiredConfigs {
		cmd := exec.Command("grep", "-q", config, "/boot/config-$(uname -r)")
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("内核缺少必要配置: %s", config)
		}
	}
	return nil
}

func CheckCRIURequirements() error {
	if err := CheckCRIUAvailable(); err != nil {
		return err
	}

	if err := CheckCRIUKernelSupport(); err != nil {
		return fmt.Errorf("内核配置检查失败: %w", err)
	}

	return nil
}

func BuildCRIUArgs(config *CRIUConfig, containerName, dir string, isDump bool) []string {
	var args []string

	if isDump {
		args = append(args, "dump")
	} else {
		args = append(args, "restore")
	}

	args = append(args,
		"--tree", fmt.Sprintf("/var/lib/lxc/%s", containerName),
		"--images-dir", dir,
		"--log-file", config.LogFile,
		"--log-level", fmt.Sprintf("%d", config.LogLevel),
	)

	if config.ShellJob {
		args = append(args, "--shell-job")
	}
	if config.ExtUnixSk {
		args = append(args, "--ext-unix-sk")
	}
	if config.TcpEstablished {
		args = append(args, "--tcp-established")
	}
	if config.FileLocks {
		args = append(args, "--file-locks")
	}
	if config.EvasiveDevices {
		args = append(args, "--evasive-devices")
	}
	if config.LinkRemap {
		args = append(args, "--link-remap")
	}

	return args
}

func PreDumpContainer(name, dir string, config *CRIUConfig) error {
	args := BuildCRIUArgs(config, name, dir, true)
	args = append(args, "--pre-dump")

	cmd := exec.Command("criu", args...)
	cmd.Stdout = exec.Command("cat").Stdout
	cmd.Stderr = exec.Command("cat").Stderr

	if err := cmd.Run(); err != nil {
		return fmt.Errorf("预dump失败: %w", err)
	}

	return nil
}

func DumpContainer(name, dir string, config *CRIUConfig) error {
	args := BuildCRIUArgs(config, name, dir, true)

	cmd := exec.Command("criu", args...)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("dump失败: %w", err)
	}

	return nil
}

func RestoreContainerFromCRIU(name, dir string, config *CRIUConfig) error {
	args := BuildCRIUArgs(config, name, dir, false)

	cmd := exec.Command("criu", args...)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("restore失败: %w", err)
	}

	return nil
}
