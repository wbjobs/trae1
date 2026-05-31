package config

import (
	"fmt"
	"os"
	"strings"
	"time"
)

type Config struct {
	GatewayPort       int
	AdminPort         int
	SPIRESocketPath   string
	ETCDEndpoints     []string
	PolicyKeyPrefix   string
	AuditKeyPrefix    string
	RotateThreshold   float64
	RotateCheckPeriod time.Duration
	SVIDCacheDir      string
	SVIDGracePeriod   time.Duration
	LogLevel          string
	TrustDomain       string
}

func Load() *Config {
	return &Config{
		GatewayPort:       intEnv("GATEWAY_PORT", 8443),
		AdminPort:         intEnv("ADMIN_PORT", 8080),
		SPIRESocketPath:   strEnv("SPIRE_SOCKET_PATH", "/tmp/spire-agent/public/api.sock"),
		ETCDEndpoints:     sliceEnv("ETCD_ENDPOINTS", []string{"localhost:2379"}),
		PolicyKeyPrefix:   strEnv("POLICY_KEY_PREFIX", "/svid-gateway/policies/"),
		AuditKeyPrefix:    strEnv("AUDIT_KEY_PREFIX", "/svid-gateway/audit/"),
		RotateThreshold:   floatEnv("ROTATE_THRESHOLD", 0.30),
		RotateCheckPeriod: durationEnv("ROTATE_CHECK_PERIOD", 1*time.Hour),
		SVIDCacheDir:      strEnv("SVID_CACHE_DIR", "./data/svid-cache"),
		SVIDGracePeriod:   durationEnv("SVID_GRACE_PERIOD", 24*time.Hour),
		LogLevel:          strEnv("LOG_LEVEL", "info"),
		TrustDomain:       strEnv("TRUST_DOMAIN", "example.org"),
	}
}

func strEnv(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}

func intEnv(k string, d int) int {
	if v := os.Getenv(k); v != "" {
		var n int
		if _, err := fmt.Sscanf(v, "%d", &n); err == nil {
			return n
		}
	}
	return d
}

func floatEnv(k string, d float64) float64 {
	if v := os.Getenv(k); v != "" {
		var f float64
		if _, err := fmt.Sscanf(v, "%f", &f); err == nil {
			return f
		}
	}
	return d
}

func sliceEnv(k string, d []string) []string {
	if v := os.Getenv(k); v != "" {
		parts := splitAndTrim(v, ",")
		if len(parts) > 0 {
			return parts
		}
	}
	return d
}

func durationEnv(k string, d time.Duration) time.Duration {
	if v := os.Getenv(k); v != "" {
		if d2, err := time.ParseDuration(v); err == nil {
			return d2
		}
	}
	return d
}

func splitAndTrim(s, sep string) []string {
	parts := strings.Split(s, sep)
	out := make([]string, 0, len(parts))
	for _, p := range parts {
		if t := strings.TrimSpace(p); t != "" {
			out = append(out, t)
		}
	}
	return out
}
