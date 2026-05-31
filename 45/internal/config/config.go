package config

import (
	"encoding/json"
	"fmt"
	"os"
	"time"
)

type Target struct {
	Address     string            `json:"address"`
	Insecure    bool              `json:"insecure"`
	TLSCertFile string            `json:"tls_cert_file,omitempty"`
	UseReflection bool            `json:"use_reflection"`
	ProtoPaths  []string          `json:"proto_paths,omitempty"`
	ProtoFiles  []string          `json:"proto_files,omitempty"`
	ImportPaths []string          `json:"import_paths,omitempty"`
	Timeout     time.Duration     `json:"-"`
	TimeoutRaw  string            `json:"timeout,omitempty"`
	Headers     map[string]string `json:"headers,omitempty"`
}

type Step struct {
	ID          string                 `json:"id"`
	Service     string                 `json:"service"`
	Method      string                 `json:"method"`
	Request     map[string]interface{} `json:"request,omitempty"`
	RequestRaw  string                 `json:"request_raw,omitempty"`
	Metadata    map[string]string      `json:"metadata,omitempty"`
	Assertions  []Assertion            `json:"assertions,omitempty"`
	DependsOn   []string               `json:"depends_on,omitempty"`
}

type Assertion struct {
	Field    string      `json:"field"`
	Operator string      `json:"op"`
	Expected interface{} `json:"expected"`
}

type Config struct {
	Target Target `json:"target"`
	Steps  []Step `json:"steps"`
}

func Load(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read config: %w", err)
	}
	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("parse config: %w", err)
	}
	if cfg.Target.TimeoutRaw != "" {
		d, err := time.ParseDuration(cfg.Target.TimeoutRaw)
		if err != nil {
			return nil, fmt.Errorf("parse timeout: %w", err)
		}
		cfg.Target.Timeout = d
	} else {
		cfg.Target.Timeout = 10 * time.Second
	}
	if err := cfg.Validate(); err != nil {
		return nil, err
	}
	return &cfg, nil
}

func (c *Config) Validate() error {
	if c.Target.Address == "" {
		return fmt.Errorf("target.address is required")
	}
	if !c.Target.UseReflection && len(c.Target.ProtoFiles) == 0 {
		return fmt.Errorf("either target.use_reflection or target.proto_files must be provided")
	}
	if len(c.Steps) == 0 {
		return fmt.Errorf("at least one step is required")
	}
	ids := map[string]bool{}
	for i, s := range c.Steps {
		if s.ID == "" {
			return fmt.Errorf("step[%d].id is required", i)
		}
		if ids[s.ID] {
			return fmt.Errorf("duplicate step id: %s", s.ID)
		}
		ids[s.ID] = true
		if s.Service == "" || s.Method == "" {
			return fmt.Errorf("step[%s].service and .method are required", s.ID)
		}
	}
	for _, s := range c.Steps {
		for _, dep := range s.DependsOn {
			if !ids[dep] {
				return fmt.Errorf("step %s depends on unknown step %s", s.ID, dep)
			}
		}
	}
	return nil
}
