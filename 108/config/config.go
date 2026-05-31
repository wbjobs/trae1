package config

import (
	"fmt"
	"os"
	"path/filepath"
	"sync"

	"gopkg.in/yaml.v3"
)

type ServiceConfig struct {
	Name            string            `yaml:"name"`
	Module          string            `yaml:"module"`
	Port            int               `yaml:"port"`
	Instances       int               `yaml:"instances"`
	EnvVars         map[string]string `yaml:"env_vars"`
	ConfigMounts    []ConfigMount     `yaml:"config_mounts"`
	HealthCheckPath string            `yaml:"health_check_path"`
	Consul          ConsulConfig      `yaml:"consul"`

	MemoryLimitMB   int    `yaml:"memory_limit_mb"`
	MaxInstructions int64  `yaml:"max_instructions"`
	TimeoutSeconds  int    `yaml:"timeout_seconds"`
	ModuleVersion   string `yaml:"module_version"`
	UseAOTCache     bool   `yaml:"use_aot_cache"`
	OptLevel        string `yaml:"opt_level"`
}

type ConfigMount struct {
	Source      string `yaml:"source"`
	Target      string `yaml:"target"`
	Permissions string `yaml:"permissions"`
}

type ConsulConfig struct {
	Enabled  bool   `yaml:"enabled"`
	Address  string `yaml:"address"`
	Port     int    `yaml:"port"`
	Service  string `yaml:"service"`
	CheckInterval string `yaml:"check_interval"`
}

type RuntimeConfig struct {
	LogLevel       string            `yaml:"log_level"`
	ListenAddr     string            `yaml:"listen_addr"`
	Services       []ServiceConfig   `yaml:"services"`
	ConsulAddress  string            `yaml:"consul_address"`
	InstanceLimit  int               `yaml:"instance_limit"`

	DefaultMemoryLimitMB   int    `yaml:"default_memory_limit_mb"`
	DefaultMaxInstructions int64  `yaml:"default_max_instructions"`
	DefaultTimeoutSeconds  int    `yaml:"default_timeout_seconds"`
	DefaultAOTCacheDir    string `yaml:"default_aot_cache_dir"`
	DefaultOptLevel       string `yaml:"default_opt_level"`
}

type ConfigManager struct {
	config     *RuntimeConfig
	configPath string
	mu         sync.RWMutex
}

func NewConfigManager(configPath string) (*ConfigManager, error) {
	data, err := os.ReadFile(configPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}

	var config RuntimeConfig
	if err := yaml.Unmarshal(data, &config); err != nil {
		return nil, fmt.Errorf("failed to parse config file: %w", err)
	}

	return &ConfigManager{
		config:     &config,
		configPath: configPath,
	}, nil
}

func (cm *ConfigManager) Get() *RuntimeConfig {
	cm.mu.RLock()
	defer cm.mu.RUnlock()
	return cm.config
}

func (cm *ConfigManager) Reload() error {
	cm.mu.Lock()
	defer cm.mu.Unlock()

	data, err := os.ReadFile(cm.configPath)
	if err != nil {
		return fmt.Errorf("failed to read config file: %w", err)
	}

	var config RuntimeConfig
	if err := yaml.Unmarshal(data, &config); err != nil {
		return fmt.Errorf("failed to parse config file: %w", err)
	}

	cm.config = &config
	return nil
}

func PrepareEnvVars(serviceEnv map[string]string, globalEnv []string) map[string]string {
	env := make(map[string]string)

	for _, e := range globalEnv {
		if idx := fmt.Index(e, "="); idx != -1 {
			env[e[:idx]] = e[idx+1:]
		}
	}

	for k, v := range serviceEnv {
		env[k] = v
	}

	return env
}

type ConfigStore struct {
	mounts    map[string]string
	mu        sync.RWMutex
}

func NewConfigStore() *ConfigStore {
	return &ConfigStore{
		mounts: make(map[string]string),
	}
}

func (cs *ConfigStore) Mount(source, target string) error {
	cs.mu.Lock()
	defer cs.mu.Unlock()

	absSource, err := filepath.Abs(source)
	if err != nil {
		return fmt.Errorf("failed to resolve config path: %w", err)
	}

	info, err := os.Stat(absSource)
	if err != nil {
		if os.IsNotExist(err) {
			return fmt.Errorf("config source does not exist: %s", absSource)
		}
		return fmt.Errorf("failed to stat config source: %w", err)
	}

	if info.IsDir() {
		files, err := os.ReadDir(absSource)
		if err != nil {
			return fmt.Errorf("failed to read config directory: %w", err)
		}
		for _, f := range files {
			fp := filepath.Join(absSource, f.Name())
			cs.mounts[fp] = filepath.Join(target, f.Name())
		}
	} else {
		cs.mounts[absSource] = target
	}

	return nil
}

func (cs *ConfigStore) GetMounts() map[string]string {
	cs.mu.RLock()
	defer cs.mu.RUnlock()
	result := make(map[string]string)
	for k, v := range cs.mounts {
		result[k] = v
	}
	return result
}

func DefaultConfig() *RuntimeConfig {
	return &RuntimeConfig{
		LogLevel:               "info",
		ListenAddr:             ":8080",
		InstanceLimit:          100,
		DefaultMemoryLimitMB:   64,
		DefaultMaxInstructions: 10_000_000,
		DefaultTimeoutSeconds:  10,
		DefaultAOTCacheDir:    "./aot-cache",
		DefaultOptLevel:       "O2",
	}
}

func (rc *RuntimeConfig) Validate() error {
	if rc.InstanceLimit <= 0 {
		rc.InstanceLimit = 100
	}
	if rc.DefaultMemoryLimitMB <= 0 {
		rc.DefaultMemoryLimitMB = 64
	}
	if rc.DefaultMaxInstructions <= 0 {
		rc.DefaultMaxInstructions = 10_000_000
	}
	if rc.DefaultTimeoutSeconds <= 0 {
		rc.DefaultTimeoutSeconds = 10
	}
	if rc.DefaultAOTCacheDir == "" {
		rc.DefaultAOTCacheDir = "./aot-cache"
	}
	if rc.DefaultOptLevel == "" {
		rc.DefaultOptLevel = "O2"
	}

	for i := range rc.Services {
		if rc.Services[i].MemoryLimitMB <= 0 {
			rc.Services[i].MemoryLimitMB = rc.DefaultMemoryLimitMB
		}
		if rc.Services[i].MaxInstructions <= 0 {
			rc.Services[i].MaxInstructions = rc.DefaultMaxInstructions
		}
		if rc.Services[i].TimeoutSeconds <= 0 {
			rc.Services[i].TimeoutSeconds = rc.DefaultTimeoutSeconds
		}
		if rc.Services[i].ModuleVersion == "" {
			rc.Services[i].ModuleVersion = "latest"
		}
		if rc.Services[i].OptLevel == "" {
			rc.Services[i].OptLevel = rc.DefaultOptLevel
		}
	}

	return nil
}
