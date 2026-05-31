package config

import (
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

type Config struct {
	MongoDB    MongoDBConfig    `yaml:"mongodb"`
	ClickHouse ClickHouseConfig `yaml:"clickhouse"`
	Tables     []TableMapping   `yaml:"tables"`
	Sync       SyncConfig       `yaml:"sync"`
	Resilience ResilienceConfig `yaml:"resilience"`
	ETL        ETLConfig        `yaml:"etl"`
}

type MongoDBConfig struct {
	URI                string         `yaml:"uri"`
	Database           string         `yaml:"database"`
	ReplicaSetMembers  []MemberConfig `yaml:"replica_set_members"`
}

type MemberConfig struct {
	URI  string `yaml:"uri"`
	Name string `yaml:"name"`
}

type ClickHouseConfig struct {
	DSN      string `yaml:"dsn"`
	Database string `yaml:"database"`
}

type TableMapping struct {
	Source       string `yaml:"source"`
	Target       string `yaml:"target"`
	PrimaryKey   string `yaml:"primary_key"`
	VersionField string `yaml:"version_field"`
	FieldMapping string `yaml:"field_mapping"`
	Pipeline     string `yaml:"pipeline"`
}

type SyncConfig struct {
	ConflictStrategy string `yaml:"conflict_strategy"`
	BatchSize       int    `yaml:"batch_size"`
	BatchTimeout    int    `yaml:"batch_timeout_ms"`
}

type ResilienceConfig struct {
	ResumePolicy    string `yaml:"resume_policy"`
	MaxReconnect   int    `yaml:"max_reconnect"`
	CacheDir       string `yaml:"cache_dir"`
	CacheMaxSizeMB int64  `yaml:"cache_max_size_mb"`
	ElectionTimeout int    `yaml:"election_timeout_ms"`
}

type ETLConfig struct {
	Enabled       bool          `yaml:"enabled"`
	PipelineFile  string        `yaml:"pipeline_file"`
	DebugServer   DebugServerConfig `yaml:"debug_server"`
	DLQ           DLQConfig     `yaml:"dlq"`
}

type DebugServerConfig struct {
	Enabled bool   `yaml:"enabled"`
	Addr    string `yaml:"addr"`
}

type DLQConfig struct {
	Type     string   `yaml:"type"`
	Brokers  []string `yaml:"brokers"`
	Topic    string   `yaml:"topic"`
	BufferSize int    `yaml:"buffer_size"`
}

type ConflictStrategy string

const (
	ConflictLastWriteWin ConflictStrategy = "last_write_win"
	ConflictIgnore      ConflictStrategy = "ignore"
	ConflictDLQ         ConflictStrategy = "dlq"
)

func LoadConfig(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}

	var cfg Config
	if err := yaml.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("failed to parse config file: %w", err)
	}

	cfg.Sync.ConflictStrategy = ConflictStrategy(cfg.Sync.ConflictStrategy)
	if cfg.Sync.ConflictStrategy == "" {
		cfg.Sync.ConflictStrategy = ConflictLastWriteWin
	}
	if cfg.Sync.BatchSize <= 0 {
		cfg.Sync.BatchSize = 100
	}
	if cfg.Sync.BatchTimeout <= 0 {
		cfg.Sync.BatchTimeout = 1000
	}

	if cfg.Resilience.ResumePolicy == "" {
		cfg.Resilience.ResumePolicy = "auto"
	}
	if cfg.Resilience.MaxReconnect <= 0 {
		cfg.Resilience.MaxReconnect = 10
	}
	if cfg.Resilience.CacheDir == "" {
		cfg.Resilience.CacheDir = "./cache"
	}
	if cfg.Resilience.CacheMaxSizeMB <= 0 {
		cfg.Resilience.CacheMaxSizeMB = 1024
	}
	if cfg.Resilience.ElectionTimeout <= 0 {
		cfg.Resilience.ElectionTimeout = 5000
	}

	if cfg.ETL.DebugServer.Addr == "" {
		cfg.ETL.DebugServer.Addr = ":8081"
	}

	return &cfg, nil
}
