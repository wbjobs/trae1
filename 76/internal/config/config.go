package config

import (
	"os"
	"strconv"
	"time"
)

type Config struct {
	SSH       SSHConfig
	MinIO     MinIOConfig
	API       APIConfig
	Audit     AuditConfig
	Ollama    OllamaConfig
	DingTalk  DingTalkConfig
	Approval  ApprovalConfig
	RecordDir string
}

type SSHConfig struct {
	ListenAddr     string
	HostKeyFile    string
	TargetHost     string
	TargetPort     int
	TargetUser     string
	TargetPassword string
	TargetKeyFile  string
}

type MinIOConfig struct {
	Endpoint      string
	AccessKey     string
	SecretKey     string
	UseSSL        bool
	BucketName    string
	SessionPrefix string
}

type APIConfig struct {
	ListenAddr    string
	SignURLExpire time.Duration
}

type AuditConfig struct {
	Enabled           bool
	SensitivePatterns []string
}

type OllamaConfig struct {
	BaseURL  string
	Model    string
	Timeout  time.Duration
	Enabled  bool
}

type DingTalkConfig struct {
	AppKey    string
	AppSecret string
	AgentID   string
	Enabled   bool
}

type ApprovalConfig struct {
	Enabled           bool
	HighRiskThreshold int
	ProcessCode       string
	Approvers         string
	Timeout           time.Duration
	FailOpen          bool
}

func Load() *Config {
	return &Config{
		SSH: SSHConfig{
			ListenAddr:     getEnv("SSH_LISTEN_ADDR", ":2222"),
			HostKeyFile:    getEnv("SSH_HOST_KEY", "host_key"),
			TargetHost:     getEnv("TARGET_HOST", "127.0.0.1"),
			TargetPort:     getEnvInt("TARGET_PORT", 22),
			TargetUser:    getEnv("TARGET_USER", "root"),
			TargetPassword: getEnv("TARGET_PASSWORD", ""),
			TargetKeyFile:   getEnv("TARGET_KEY_FILE", ""),
		},
		MinIO: MinIOConfig{
			Endpoint:   getEnv("MINIO_ENDPOINT", "127.0.0.1:9000"),
			AccessKey:  getEnv("MINIO_ACCESS_KEY", "minioadmin"),
			SecretKey:  getEnv("MINIO_SECRET_KEY", "minioadmin"),
			UseSSL:     getEnvBool("MINIO_USE_SSL", false),
			BucketName: getEnv("MINIO_BUCKET", "bastion-sessions"),
			SessionPrefix: getEnv("MINIO_SESSION_PREFIX", "sessions/"),
		},
		API: APIConfig{
			ListenAddr:    getEnv("API_LISTEN_ADDR", ":8080"),
			SignURLExpire:  getEnvDuration("API_SIGN_URL_EXPIRE", 1*time.Hour),
		},
		Audit: AuditConfig{
			Enabled: getEnvBool("AUDIT_ENABLED", true),
			SensitivePatterns: []string{
				"rm -rf /",
				"rm -rf /*",
				"chmod 777",
				"chmod -R 777",
				"mkfs",
				"dd if=",
				"> /dev/sd",
				"format c:",
				"shutdown",
				"reboot",
				"halt",
				"init 0",
				"poweroff",
			},
		},
		Ollama: OllamaConfig{
			BaseURL: getEnv("OLLAMA_BASE_URL", "http://127.0.0.1:11434"),
			Model:   getEnv("OLLAMA_MODEL", "qwen2.5-coder:7b"),
			Timeout: getEnvDuration("OLLAMA_TIMEOUT", 30*time.Second),
			Enabled: getEnvBool("OLLAMA_ENABLED", true),
		},
		DingTalk: DingTalkConfig{
			AppKey:    getEnv("DINGTALK_APP_KEY", ""),
			AppSecret: getEnv("DINGTALK_APP_SECRET", ""),
			AgentID:   getEnv("DINGTALK_AGENT_ID", ""),
			Enabled:   getEnvBool("DINGTALK_ENABLED", false),
		},
		Approval: ApprovalConfig{
			Enabled:           getEnvBool("APPROVAL_ENABLED", true),
			HighRiskThreshold: getEnvInt("APPROVAL_HIGH_RISK_THRESHOLD", 4),
			ProcessCode:       getEnv("APPROVAL_PROCESS_CODE", "PROC-COMMAND-APPROVAL"),
			Approvers:         getEnv("APPROVAL_APPROVERS", ""),
			Timeout:           getEnvDuration("APPROVAL_TIMEOUT", 5*time.Minute),
			FailOpen:          getEnvBool("APPROVAL_FAIL_OPEN", false),
		},
		RecordDir: getEnv("RECORD_DIR", "./recordings"),
	}
}

func getEnv(key, def string) string {
	if v := os.Getenv(key); v != "" {
		return v
	}
	return def
}

func getEnvInt(key string, def int) int {
	if v := os.Getenv(key); v != "" {
		if i, err := strconv.Atoi(v); err == nil {
			return i
		}
	}
	return def
}

func getEnvBool(key string, def bool) bool {
	if v := os.Getenv(key); v != "" {
		if b, err := strconv.ParseBool(v); err == nil {
			return b
		}
	}
	return def
}

func getEnvDuration(key string, def time.Duration) time.Duration {
	if v := os.Getenv(key); v != "" {
		if d, err := time.ParseDuration(v); err == nil {
			return d
		}
	}
	return def
}
