package config

import (
	"fmt"
	"os"
	"strconv"
	"time"
)

type Config struct {
	Server     ServerConfig
	Sandbox    SandboxConfig
	Resources  ResourceConfig
	Security   SecurityConfig
	AISecurity AISecurityConfig
	Logging    LoggingConfig
}

type AISecurityConfig struct {
	Enabled           bool
	OllamaURL         string
	Model             string
	RequestTimeout    time.Duration
	CacheTTL          time.Duration
	RejectThreshold   int
	ApproveThreshold  int
	ApprovalWebhookURL string
	ApprovalTimeout   time.Duration
}

type ServerConfig struct {
	Port         int
	ReadTimeout  time.Duration
	WriteTimeout time.Duration
}

type SandboxConfig struct {
	RunscBinPath   string
	RootDir        string
	InputDir       string
	OutputDir      string
	WorkDir        string
	MaxConcurrent  int
	DefaultTimeout time.Duration
}

type ResourceConfig struct {
	CPULimit     float64
	MemoryLimit  int64
	PIDLimit     int
	OOMScoreAdj  int
	IOReadLimit  int64
	IOWriteLimit int64
	NetworkMode  string
}

type SecurityConfig struct {
	AllowRoot     bool
	ReadOnlyFS    bool
	DropCaps      []string
	DisableNetwork bool
}

type LoggingConfig struct {
	Level      string
	OutputPath string
	MaxSize    int
	MaxBackups int
	MaxAge     int
}

func Load() (*Config, error) {
	cfg := &Config{
		Server: ServerConfig{
			Port:         getEnvInt("SERVER_PORT", 8080),
			ReadTimeout:  getEnvDuration("SERVER_READ_TIMEOUT", 30*time.Second),
			WriteTimeout: getEnvDuration("SERVER_WRITE_TIMEOUT", 30*time.Second),
		},
		Sandbox: SandboxConfig{
			RunscBinPath:   getEnv("RUNSC_BIN_PATH", "/usr/local/bin/runsc"),
			RootDir:        getEnv("SANDBOX_ROOT_DIR", "/sandbox"),
			InputDir:       getEnv("SANDBOX_INPUT_DIR", "/sandbox/input"),
			OutputDir:      getEnv("SANDBOX_OUTPUT_DIR", "/sandbox/output"),
			WorkDir:        getEnv("SANDBOX_WORK_DIR", "/sandbox/work"),
			MaxConcurrent:  getEnvInt("SANDBOX_MAX_CONCURRENT", 10),
			DefaultTimeout: getEnvDuration("SANDBOX_DEFAULT_TIMEOUT", 30*time.Second),
		},
		Resources: ResourceConfig{
			CPULimit:     getEnvFloat("RESOURCE_CPU_LIMIT", 1.0),
			MemoryLimit:  getEnvInt64("RESOURCE_MEMORY_LIMIT", 512*1024*1024),
			PIDLimit:     getEnvInt("RESOURCE_PID_LIMIT", 100),
			OOMScoreAdj:  getEnvInt("RESOURCE_OOM_SCORE_ADJ", 1000),
			IOReadLimit:  getEnvInt64("RESOURCE_IO_READ_LIMIT", 10*1024*1024),
			IOWriteLimit: getEnvInt64("RESOURCE_IO_WRITE_LIMIT", 10*1024*1024),
			NetworkMode:  getEnv("RESOURCE_NETWORK_MODE", "none"),
		},
		Security: SecurityConfig{
			AllowRoot:      false,
			ReadOnlyFS:     true,
			DropCaps:       []string{"ALL"},
			DisableNetwork: true,
		},
		AISecurity: AISecurityConfig{
			Enabled:           getEnvBool("AI_SECURITY_ENABLED", true),
			OllamaURL:         getEnv("AI_SECURITY_OLLAMA_URL", "http://localhost:11434"),
			Model:             getEnv("AI_SECURITY_MODEL", "qwen2.5-coder:7b-instruct"),
			RequestTimeout:    getEnvDuration("AI_SECURITY_REQUEST_TIMEOUT", 30*time.Second),
			CacheTTL:          getEnvDuration("AI_SECURITY_CACHE_TTL", 1*time.Hour),
			RejectThreshold:   getEnvInt("AI_SECURITY_REJECT_THRESHOLD", 60),
			ApproveThreshold:  getEnvInt("AI_SECURITY_APPROVE_THRESHOLD", 90),
			ApprovalWebhookURL: getEnv("AI_SECURITY_APPROVAL_WEBHOOK_URL", ""),
			ApprovalTimeout:   getEnvDuration("AI_SECURITY_APPROVAL_TIMEOUT", 5*time.Minute),
		},
		Logging: LoggingConfig{
			Level:      getEnv("LOG_LEVEL", "info"),
			OutputPath: getEnv("LOG_OUTPUT_PATH", "/var/log/sandbox"),
			MaxSize:    getEnvInt("LOG_MAX_SIZE", 100),
			MaxBackups: getEnvInt("LOG_MAX_BACKUPS", 3),
			MaxAge:     getEnvInt("LOG_MAX_AGE", 7),
		},
	}

	if err := cfg.Validate(); err != nil {
		return nil, fmt.Errorf("config validation failed: %w", err)
	}

	return cfg, nil
}

func (c *Config) Validate() error {
	if c.Server.Port <= 0 || c.Server.Port > 65535 {
		return fmt.Errorf("invalid server port: %d", c.Server.Port)
	}
	if c.Resources.CPULimit <= 0 {
		return fmt.Errorf("CPU limit must be positive: %f", c.Resources.CPULimit)
	}
	if c.Resources.MemoryLimit <= 0 {
		return fmt.Errorf("memory limit must be positive: %d", c.Resources.MemoryLimit)
	}
	if c.Resources.PIDLimit <= 0 {
		return fmt.Errorf("PID limit must be positive: %d", c.Resources.PIDLimit)
	}
	if c.Resources.OOMScoreAdj < -1000 || c.Resources.OOMScoreAdj > 1000 {
		return fmt.Errorf("OOM score adjust must be between -1000 and 1000: %d", c.Resources.OOMScoreAdj)
	}
	if c.Sandbox.MaxConcurrent <= 0 {
		return fmt.Errorf("max concurrent must be positive: %d", c.Sandbox.MaxConcurrent)
	}
	return nil
}

func getEnv(key, defaultVal string) string {
	if val, ok := os.LookupEnv(key); ok {
		return val
	}
	return defaultVal
}

func getEnvInt(key string, defaultVal int) int {
	if val, ok := os.LookupEnv(key); ok {
		if v, err := strconv.Atoi(val); err == nil {
			return v
		}
	}
	return defaultVal
}

func getEnvInt64(key string, defaultVal int64) int64 {
	if val, ok := os.LookupEnv(key); ok {
		if v, err := strconv.ParseInt(val, 10, 64); err == nil {
			return v
		}
	}
	return defaultVal
}

func getEnvFloat(key string, defaultVal float64) float64 {
	if val, ok := os.LookupEnv(key); ok {
		if v, err := strconv.ParseFloat(val, 64); err == nil {
			return v
		}
	}
	return defaultVal
}

func getEnvDuration(key string, defaultVal time.Duration) time.Duration {
	if val, ok := os.LookupEnv(key); ok {
		if v, err := time.ParseDuration(val); err == nil {
			return v
		}
	}
	return defaultVal
}

func getEnvBool(key string, defaultVal bool) bool {
	if val, ok := os.LookupEnv(key); ok {
		if v, err := strconv.ParseBool(val); err == nil {
			return v
		}
	}
	return defaultVal
}
