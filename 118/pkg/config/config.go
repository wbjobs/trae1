package config

import (
	"encoding/json"
	"fmt"
	"os"
)

type Config struct {
	Daemon DaemonConfig `json:"daemon"`
	Migrate MigrateConfig `json:"migrate"`
	CRIU    CRIUConfig    `json:"criu"`
}

type DaemonConfig struct {
	Port    int    `json:"port"`
	DataDir string `json:"data_dir"`
	LogFile string `json:"log_file"`
}

type MigrateConfig struct {
	DefaultPort  int    `json:"default_port"`
	CheckpointDir string `json:"checkpoint_dir"`
	DefaultBwlimit int  `json:"default_bwlimit"`
	PreCopyIter   int    `json:"pre_copy_iter"`
}

type CRIUConfig struct {
	LogFile        string `json:"log_file"`
	LogLevel       int    `json:"log_level"`
	ShellJob       bool   `json:"shell_job"`
	TcpEstablished bool   `json:"tcp_established"`
	FileLocks      bool   `json:"file_locks"`
}

func DefaultConfig() *Config {
	return &Config{
		Daemon: DaemonConfig{
			Port:    9999,
			DataDir: "/tmp/lxc-migrate",
			LogFile: "/var/log/lxc-migrate.log",
		},
		Migrate: MigrateConfig{
			DefaultPort:   9999,
			CheckpointDir: "/tmp/lxc-migrate",
			DefaultBwlimit: 0,
			PreCopyIter:   3,
		},
		CRIU: CRIUConfig{
			LogFile:        "criu.log",
			LogLevel:       2,
			ShellJob:       true,
			TcpEstablished: true,
			FileLocks:      true,
		},
	}
}

func LoadConfig(path string) (*Config, error) {
	if path == "" {
		path = "/etc/lxc-migrate/config.json"
	}

	if _, err := os.Stat(path); os.IsNotExist(err) {
		config := DefaultConfig()
		if err := SaveConfig(path, config); err != nil {
			return nil, err
		}
		return config, nil
	}

	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("读取配置文件失败: %w", err)
	}

	var config Config
	if err := json.Unmarshal(data, &config); err != nil {
		return nil, fmt.Errorf("解析配置文件失败: %w", err)
	}

	return &config, nil
}

func SaveConfig(path string, config *Config) error {
	dir := ""
	if idx := lastSlash(path); idx >= 0 {
		dir = path[:idx]
	}

	if dir != "" {
		if err := os.MkdirAll(dir, 0755); err != nil {
			return fmt.Errorf("创建配置目录失败: %w", err)
		}
	}

	data, err := json.MarshalIndent(config, "", "  ")
	if err != nil {
		return fmt.Errorf("序列化配置失败: %w", err)
	}

	return os.WriteFile(path, data, 0644)
}

func lastSlash(s string) int {
	for i := len(s) - 1; i >= 0; i-- {
		if s[i] == '/' || s[i] == '\\' {
			return i
		}
	}
	return -1
}
