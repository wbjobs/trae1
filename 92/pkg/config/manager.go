package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/io-qos/io-qos/internal/types"
	"github.com/io-qos/io-qos/pkg/cgroup"
	"github.com/io-qos/io-qos/pkg/container"
	"gopkg.in/yaml.v3"
)

const (
	rollbackDir = ".io-qos-rollback"
)

type Manager struct {
	cgroupRoot string
	ioCtl      *cgroup.IOController
	discoverer *container.Discoverer
}

func NewManager(cgroupRoot string) *Manager {
	return &Manager{
		cgroupRoot: cgroupRoot,
		ioCtl:      cgroup.NewIOControllerWithRoot(cgroupRoot),
		discoverer: container.NewDiscovererWithRoot(cgroupRoot),
	}
}

func (m *Manager) LoadConfig(path string) (*types.ConfigFile, error) {
	content, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config file: %w", err)
	}

	var cfg types.ConfigFile
	if err := yaml.Unmarshal(content, &cfg); err != nil {
		return nil, fmt.Errorf("failed to parse config file: %w", err)
	}

	if cfg.Version == "" {
		cfg.Version = "1.0"
	}

	for i := range cfg.Rules {
		if cfg.Defaults != nil {
			if cfg.Rules[i].Limits.ReadBPS == 0 {
				cfg.Rules[i].Limits.ReadBPS = cfg.Defaults.ReadBPS
			}
			if cfg.Rules[i].Limits.WriteBPS == 0 {
				cfg.Rules[i].Limits.WriteBPS = cfg.Defaults.WriteBPS
			}
			if cfg.Rules[i].Limits.ReadIOPS == 0 {
				cfg.Rules[i].Limits.ReadIOPS = cfg.Defaults.ReadIOPS
			}
			if cfg.Rules[i].Limits.WriteIOPS == 0 {
				cfg.Rules[i].Limits.WriteIOPS = cfg.Defaults.WriteIOPS
			}
			if cfg.Rules[i].Limits.Priority == "" {
				cfg.Rules[i].Limits.Priority = cfg.Defaults.Priority
			}
		}
	}

	return &cfg, nil
}

func (m *Manager) SaveConfig(path string, cfg *types.ConfigFile) error {
	content, err := yaml.Marshal(cfg)
	if err != nil {
		return fmt.Errorf("failed to marshal config: %w", err)
	}

	if err := os.WriteFile(path, content, 0644); err != nil {
		return fmt.Errorf("failed to write config file: %w", err)
	}

	return nil
}

func (m *Manager) ApplyConfig(cfg *types.ConfigFile, dryRun bool) (*types.RollbackPoint, error) {
	rollback := &types.RollbackPoint{
		Timestamp:     time.Now(),
		PreviousState: make(map[string]types.IOLimit),
		AppliedRules:  make([]types.ContainerConfig, 0),
	}

	for i, rule := range cfg.Rules {
		resolved, err := container.ResolveConfig(rule, m.cgroupRoot)
		if err != nil {
			return nil, fmt.Errorf("rule %d: failed to resolve container %s: %w", i, rule.ContainerID, err)
		}
		cfg.Rules[i] = resolved

		oldLimits, err := m.ioCtl.GetCurrentLimits(resolved.CgroupPath)
		if err == nil {
			rollback.PreviousState[resolved.ContainerID] = oldLimits
		}

		if !dryRun {
			if err := m.ioCtl.ApplyLimits(resolved.CgroupPath, resolved.Limits); err != nil {
				return rollback, fmt.Errorf("rule %d: failed to apply limits to %s: %w", i, resolved.ContainerID, err)
			}
		}

		rollback.AppliedRules = append(rollback.AppliedRules, resolved)
	}

	if !dryRun {
		if err := m.saveRollbackPoint(rollback); err != nil {
			return rollback, fmt.Errorf("failed to save rollback point: %w", err)
		}
	}

	return rollback, nil
}

func (m *Manager) Rollback(rollbackPoint *types.RollbackPoint) error {
	var errs []string

	for containerID, oldLimits := range rollbackPoint.PreviousState {
		resolved, err := container.ResolveConfig(types.ContainerConfig{
			ContainerID: containerID,
			Limits:      oldLimits,
		}, m.cgroupRoot)

		if err != nil {
			errs = append(errs, fmt.Sprintf("container %s: %v", containerID, err))
			continue
		}

		if err := m.ioCtl.ApplyLimits(resolved.CgroupPath, oldLimits); err != nil {
			errs = append(errs, fmt.Sprintf("container %s: %v", containerID, err))
		}
	}

	if len(errs) > 0 {
		return fmt.Errorf("rollback partially failed: %s", fmt.Sprintf("%v", errs))
	}

	return nil
}

func (m *Manager) RollbackLast() (*types.RollbackPoint, error) {
	rollbackPoint, err := m.loadLastRollbackPoint()
	if err != nil {
		return nil, err
	}

	if err := m.Rollback(rollbackPoint); err != nil {
		return rollbackPoint, err
	}

	return rollbackPoint, nil
}

func (m *Manager) saveRollbackPoint(rb *types.RollbackPoint) error {
	if err := os.MkdirAll(rollbackDir, 0755); err != nil {
		return err
	}

	filename := filepath.Join(rollbackDir, fmt.Sprintf("rollback-%d.json", rb.Timestamp.Unix()))
	content, err := json.MarshalIndent(rb, "", "  ")
	if err != nil {
		return err
	}

	return os.WriteFile(filename, content, 0644)
}

func (m *Manager) loadLastRollbackPoint() (*types.RollbackPoint, error) {
	entries, err := os.ReadDir(rollbackDir)
	if err != nil {
		return nil, fmt.Errorf("no rollback points found: %w", err)
	}

	var latestFile string
	var latestTime int64

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if !filepath.IsAbs(name) {
			name = filepath.Join(rollbackDir, name)
		}

		var ts int64
		_, err := fmt.Sscanf(filepath.Base(name), "rollback-%d.json", &ts)
		if err != nil {
			continue
		}

		if ts > latestTime {
			latestTime = ts
			latestFile = name
		}
	}

	if latestFile == "" {
		return nil, fmt.Errorf("no rollback points found")
	}

	content, err := os.ReadFile(latestFile)
	if err != nil {
		return nil, err
	}

	var rb types.RollbackPoint
	if err := json.Unmarshal(content, &rb); err != nil {
		return nil, err
	}

	return &rb, nil
}

func (m *Manager) ListRollbackPoints() ([]types.RollbackPoint, error) {
	entries, err := os.ReadDir(rollbackDir)
	if err != nil {
		return nil, err
	}

	var points []types.RollbackPoint

	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if !filepath.IsAbs(name) {
			name = filepath.Join(rollbackDir, name)
		}

		content, err := os.ReadFile(name)
		if err != nil {
			continue
		}

		var rb types.RollbackPoint
		if err := json.Unmarshal(content, &rb); err != nil {
			continue
		}

		points = append(points, rb)
	}

	return points, nil
}

func (m *Manager) GenerateTemplate(path string) error {
	cfg := &types.ConfigFile{
		Version: "1.0",
		Defaults: &types.IOLimit{
			ReadBPS:   0,
			WriteBPS:  0,
			ReadIOPS:  0,
			WriteIOPS: 0,
			Priority:  types.PriorityMedium,
		},
		Rules: []types.ContainerConfig{
			{
				ContainerID: "container-id-1",
				Limits: types.IOLimit{
					WriteBPS:  10 * 1024 * 1024,
					Priority:  types.PriorityHigh,
				},
			},
			{
				ContainerID: "container-id-2",
				CgroupPath:  "/sys/fs/cgroup/docker/abc123def456",
				Limits: types.IOLimit{
					ReadIOPS:  1000,
					WriteIOPS: 500,
					Priority:   types.PriorityLow,
				},
			},
		},
	}

	return m.SaveConfig(path, cfg)
}
