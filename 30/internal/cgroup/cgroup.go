package cgroup

import (
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"

	"transcode-gateway/internal/config"
	"transcode-gateway/internal/logger"
	"transcode-gateway/internal/model"
)

type Manager struct {
	cfg *config.Config
	log *logger.Logger

	v1Path   string
	v2Path   string
	useV2    bool

	groups map[string]string
	mu     sync.RWMutex
}

func New(cfg *config.Config, log *logger.Logger) *Manager {
	m := &Manager{
		cfg:    cfg,
		log:    log,
		groups: make(map[string]string),
	}

	if !cfg.CGroups.Enabled {
		log.Warn("cgroups 未启用，CPU 资源控制跳过")
		return m
	}

	if runtime.GOOS != "linux" {
		log.Warnf("cgroups 仅支持 Linux，当前系统 %s，跳过", runtime.GOOS)
		cfg.CGroups.Enabled = false
		return m
	}

	m.v1Path = cfg.CGroups.V1MountPath
	m.v2Path = cfg.CGroups.V2MountPath

	if _, err := os.Stat(filepath.Join(m.v2Path, "cgroup.controllers")); err == nil {
		m.useV2 = true
		log.Info("检测到 cgroup v2")
	} else if _, err := os.Stat(filepath.Join(m.v1Path, "cpu", "cfs_quota_us")); err == nil {
		m.useV2 = false
		log.Info("检测到 cgroup v1")
	} else {
		log.Warn("未检测到 cgroup 文件系统，CPU 资源控制跳过")
		cfg.CGroups.Enabled = false
	}

	return m
}

func (m *Manager) AssignTask(taskID string, pid int, priority model.Priority) error {
	if !m.cfg.CGroups.Enabled {
		return nil
	}

	groupName := fmt.Sprintf("transcode-%s-%s", priority.String(), taskID)

	if m.useV2 {
		return m.assignV2(groupName, pid, priority)
	}
	return m.assignV1(groupName, pid, priority)
}

func (m *Manager) assignV2(groupName string, pid int, priority model.Priority) error {
	groupPath := filepath.Join(m.v2Path, groupName)
	if err := os.MkdirAll(groupPath, 0o755); err != nil {
		return fmt.Errorf("创建 cgroup v2 目录失败: %w", err)
	}

	controllersPath := filepath.Join(m.v2Path, "cgroup.subtree_control")
	controllers, err := os.ReadFile(controllersPath)
	if err == nil {
		ctrls := strings.TrimSpace(string(controllers))
		if !strings.Contains(ctrls, "cpu") {
			if err := os.WriteFile(controllersPath, []byte("+cpu"), 0o644); err != nil {
				m.log.Warnf("启用 cpu controller 失败（需要 root 权限）: %v", err)
			}
		}
	}

	quota := m.quotaForPriority(priority)
	if quota != "-1" {
		cpuMaxPath := filepath.Join(groupPath, "cpu.max")
		if err := os.WriteFile(cpuMaxPath, []byte(quota), 0o644); err != nil {
			return fmt.Errorf("设置 cpu.max 失败: %w", err)
		}
	}

	procsPath := filepath.Join(groupPath, "cgroup.procs")
	if err := os.WriteFile(procsPath, []byte(strconv.Itoa(pid)), 0o644); err != nil {
		return fmt.Errorf("加入 cgroup v2 失败: %w", err)
	}

	m.mu.Lock()
	m.groups[fmt.Sprintf("%d", pid)] = groupPath
	m.mu.Unlock()

	m.log.Infof("进程 %d 加入 cgroup v2: %s (quota=%s)", pid, groupPath, quota)
	return nil
}

func (m *Manager) assignV1(groupName string, pid int, priority model.Priority) error {
	cpuPath := filepath.Join(m.v1Path, "cpu", groupName)
	if err := os.MkdirAll(cpuPath, 0o755); err != nil {
		return fmt.Errorf("创建 cgroup v1 cpu 目录失败: %w", err)
	}

	sharesPath := filepath.Join(cpuPath, "cpu.shares")
	shares := m.sharesForPriority(priority)
	if err := os.WriteFile(sharesPath, []byte(strconv.Itoa(shares)), 0o644); err != nil {
		return fmt.Errorf("设置 cpu.shares 失败: %w", err)
	}

	quota := m.quotaForPriority(priority)
	if quota != "-1" {
		quotaPath := filepath.Join(cpuPath, "cpu.cfs_quota_us")
		quotaUs := m.parseQuota(quota)
		if quotaUs > 0 {
			if err := os.WriteFile(quotaPath, []byte(strconv.Itoa(quotaUs)), 0o644); err != nil {
				return fmt.Errorf("设置 cpu.cfs_quota_us 失败: %w", err)
			}
		}
	}

	tasksPath := filepath.Join(cpuPath, "tasks")
	if err := os.WriteFile(tasksPath, []byte(strconv.Itoa(pid)), 0o644); err != nil {
		return fmt.Errorf("加入 cgroup v1 失败: %w", err)
	}

	m.mu.Lock()
	m.groups[fmt.Sprintf("%d", pid)] = cpuPath
	m.mu.Unlock()

	m.log.Infof("进程 %d 加入 cgroup v1: %s (shares=%d, quota=%s)", pid, cpuPath, shares, quota)
	return nil
}

func (m *Manager) ReleaseTask(pid int) error {
	if !m.cfg.CGroups.Enabled {
		return nil
	}

	pidStr := fmt.Sprintf("%d", pid)
	m.mu.RLock()
	groupPath, ok := m.groups[pidStr]
	m.mu.RUnlock()

	if !ok {
		return nil
	}

	if m.useV2 {
		procsPath := filepath.Join(m.v2Path, "cgroup.procs")
		_ = os.WriteFile(procsPath, []byte(pidStr), 0o644)
	} else {
		rootTasks := filepath.Join(m.v1Path, "cpu", "tasks")
		_ = os.WriteFile(rootTasks, []byte(pidStr), 0o644)
	}

	_ = os.RemoveAll(groupPath)

	m.mu.Lock()
	delete(m.groups, pidStr)
	m.mu.Unlock()

	m.log.Infof("进程 %d 已从 cgroup 释放: %s", pid, groupPath)
	return nil
}

func (m *Manager) CleanupAll() {
	if !m.cfg.CGroups.Enabled {
		return
	}

	m.mu.Lock()
	paths := make([]string, 0, len(m.groups))
	for _, p := range m.groups {
		paths = append(paths, p)
	}
	m.groups = make(map[string]string)
	m.mu.Unlock()

	for _, p := range paths {
		_ = os.RemoveAll(p)
	}

	m.log.Info("所有 cgroup 已清理")
}

func (m *Manager) sharesForPriority(p model.Priority) int {
	switch p {
	case model.PriorityHigh:
		return m.cfg.CGroups.HighCPUShares
	case model.PriorityMedium:
		return m.cfg.CGroups.MediumCPUShares
	case model.PriorityLow:
		return m.cfg.CGroups.LowCPUShares
	}
	return m.cfg.CGroups.MediumCPUShares
}

func (m *Manager) quotaForPriority(p model.Priority) string {
	switch p {
	case model.PriorityHigh:
		return m.cfg.CGroups.HighCPUQuota
	case model.PriorityMedium:
		return m.cfg.CGroups.MediumCPUQuota
	case model.PriorityLow:
		return m.cfg.CGroups.LowCPUQuota
	}
	return m.cfg.CGroups.MediumCPUQuota
}

func (m *Manager) parseQuota(q string) int {
	if q == "-1" || q == "" {
		return -1
	}
	if strings.HasSuffix(q, "%") {
		pctStr := strings.TrimSuffix(q, "%")
		pct, err := strconv.Atoi(pctStr)
		if err != nil {
			return -1
		}
		period := 100000
		return (pct * period) / 100
	}
	v, err := strconv.Atoi(q)
	if err != nil {
		return -1
	}
	return v
}
