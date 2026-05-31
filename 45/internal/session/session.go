package session

import (
	"fmt"
	"os"
	"time"

	"gopkg.in/yaml.v3"
)

type Entry struct {
	ID        string                 `yaml:"id"`
	Service   string                 `yaml:"service"`
	Method    string                 `yaml:"method"`
	Metadata  map[string]string      `yaml:"metadata,omitempty"`
	Request   map[string]interface{} `yaml:"request"`
	Response  map[string]interface{} `yaml:"response"`
	Duration  time.Duration          `yaml:"duration"`
	Recorded  time.Time              `yaml:"recorded_at"`
	Ignore    []string               `yaml:"ignore,omitempty"`
	Error     string                 `yaml:"error,omitempty"`
}

type Session struct {
	Version  int       `yaml:"version"`
	Target   string    `yaml:"target"`
	Insecure bool      `yaml:"insecure"`
	Timeout  string    `yaml:"timeout,omitempty"`
	UseReflect bool    `yaml:"use_reflection"`
	ProtoFiles []string `yaml:"proto_files,omitempty"`
	ImportPaths []string `yaml:"import_paths,omitempty"`
	Created  time.Time `yaml:"created_at"`
	Entries  []Entry   `yaml:"entries"`
}

func Save(path string, sess *Session) error {
	data, err := yaml.Marshal(sess)
	if err != nil {
		return fmt.Errorf("marshal session: %w", err)
	}
	if err := os.WriteFile(path, data, 0644); err != nil {
		return fmt.Errorf("write session: %w", err)
	}
	return nil
}

func Load(path string) (*Session, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read session: %w", err)
	}
	var s Session
	if err := yaml.Unmarshal(data, &s); err != nil {
		return nil, fmt.Errorf("parse session: %w", err)
	}
	if s.Version == 0 {
		s.Version = 1
	}
	return &s, nil
}
