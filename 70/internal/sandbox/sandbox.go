package sandbox

import (
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"time"

	"github.com/google/uuid"
	"go.uber.org/zap"

	"github.com/sandbox/executor/internal/config"
	"github.com/sandbox/executor/internal/stats"
)

type Sandbox struct {
	ID         string
	RootDir    string
	InputDir   string
	OutputDir  string
	WorkDir    string
	Status     SandboxStatus
	CreatedAt  time.Time
	FinishedAt *time.Time
	cmd        *exec.Cmd
	cancelFunc context.CancelFunc
	mu         sync.RWMutex
	logger     *zap.Logger
	stats      *stats.ResourceStats
}

type SandboxStatus string

const (
	SandboxStatusPending   SandboxStatus = "pending"
	SandboxStatusRunning   SandboxStatus = "running"
	SandboxStatusCompleted SandboxStatus = "completed"
	SandboxStatusFailed    SandboxStatus = "failed"
	SandboxStatusTimeout   SandboxStatus = "timeout"
	SandboxStatusDestroyed SandboxStatus = "destroyed"
)

type ExecutionResult struct {
	ExitCode    int
	Stdout      string
	Stderr      string
	ExecutionTime time.Duration
	Stats       *stats.ResourceStats
}

type Manager struct {
	cfg       *config.Config
	logger    *zap.Logger
	sandboxes map[string]*Sandbox
	mu        sync.RWMutex
	sem       chan struct{}
	collector *stats.Collector
}

func NewManager(cfg *config.Config, logger *zap.Logger) *Manager {
	return &Manager{
		cfg:       cfg,
		logger:    logger,
		sandboxes: make(map[string]*Sandbox),
		sem:       make(chan struct{}, cfg.Sandbox.MaxConcurrent),
		collector: stats.NewCollector(logger),
	}
}

func (m *Manager) CreateSandbox(ctx context.Context) (*Sandbox, error) {
	select {
	case m.sem <- struct{}{}:
	case <-ctx.Done():
		return nil, ctx.Err()
	}

	sbID := uuid.New().String()
	rootDir := filepath.Join(m.cfg.Sandbox.RootDir, sbID)

	sb := &Sandbox{
		ID:        sbID,
		RootDir:   rootDir,
		InputDir:  filepath.Join(rootDir, "input"),
		OutputDir: filepath.Join(rootDir, "output"),
		WorkDir:   filepath.Join(rootDir, "work"),
		Status:    SandboxStatusPending,
		CreatedAt: time.Now(),
		logger:    logger.With(zap.String("sandbox_id", sbID)),
		stats:     stats.NewResourceStats(),
	}

	dirs := []string{sb.RootDir, sb.InputDir, sb.OutputDir, sb.WorkDir}
	for _, dir := range dirs {
		if err := os.MkdirAll(dir, 0750); err != nil {
			m.cleanupFailedSandbox(sb)
			return nil, fmt.Errorf("failed to create directory %s: %w", dir, err)
		}
	}

	sb.logger.Info("Sandbox created",
		zap.String("root_dir", sb.RootDir),
		zap.String("input_dir", sb.InputDir),
		zap.String("output_dir", sb.OutputDir),
		zap.String("work_dir", sb.WorkDir),
	)

	m.mu.Lock()
	m.sandboxes[sbID] = sb
	m.mu.Unlock()

	return sb, nil
}

func (m *Manager) Execute(ctx context.Context, sb *Sandbox, command string, args []string, timeout time.Duration) (*ExecutionResult, error) {
	sb.mu.Lock()
	if sb.Status != SandboxStatusPending {
		sb.mu.Unlock()
		return nil, fmt.Errorf("sandbox %s is not in pending state (current: %s)", sb.ID, sb.Status)
	}
	sb.Status = SandboxStatusRunning
	sb.mu.Unlock()

	if timeout <= 0 {
		timeout = m.cfg.Sandbox.DefaultTimeout
	}

	execCtx, cancel := context.WithTimeout(ctx, timeout)
	sb.cancelFunc = cancel
	defer cancel()

	result := &ExecutionResult{
		Stats: sb.stats,
	}
	startTime := time.Now()

	runscArgs := m.buildRunscArgs(sb, command, args)

	sb.logger.Info("Executing command in sandbox",
		zap.String("command", command),
		zap.Strings("args", args),
		zap.Strings("runsc_args", runscArgs),
	)

	go m.collector.StartCollecting(execCtx, sb.stats, sb.RootDir)

	cmd := exec.CommandContext(execCtx, m.cfg.Sandbox.RunscBinPath, runscArgs...)
	sb.cmd = cmd
	sb.cmd.Dir = sb.WorkDir

	stdoutPipe, err := cmd.StdoutPipe()
	if err != nil {
		m.failSandbox(sb, startTime)
		return nil, fmt.Errorf("failed to create stdout pipe: %w", err)
	}

	stderrPipe, err := cmd.StderrPipe()
	if err != nil {
		m.failSandbox(sb, startTime)
		return nil, fmt.Errorf("failed to create stderr pipe: %w", err)
	}

	if err := cmd.Start(); err != nil {
		m.failSandbox(sb, startTime)
		return nil, fmt.Errorf("failed to start sandbox: %w", err)
	}

	if err := m.setOOMScoreAdj(cmd.Process.Pid, m.cfg.Resources.OOMScoreAdj); err != nil {
		sb.logger.Warn("Failed to set OOM score adjust",
			zap.Int("pid", cmd.Process.Pid),
			zap.Int("oom_score_adj", m.cfg.Resources.OOMScoreAdj),
			zap.Error(err),
		)
	}

	stdoutData, stderrData := m.readOutputs(stdoutPipe, stderrPipe)

	if err := cmd.Wait(); err != nil {
		sb.mu.Lock()
		if execCtx.Err() == context.DeadlineExceeded {
			sb.Status = SandboxStatusTimeout
			result.ExitCode = -1
			sb.logger.Warn("Sandbox execution timed out",
				zap.Duration("timeout", timeout),
			)
		} else {
			sb.Status = SandboxStatusFailed
			result.ExitCode = -1
			sb.logger.Error("Sandbox execution failed",
				zap.Error(err),
			)
		}
		sb.mu.Unlock()
	} else {
		sb.mu.Lock()
		sb.Status = SandboxStatusCompleted
		result.ExitCode = cmd.ProcessState.ExitCode()
		sb.mu.Unlock()
	}

	finishTime := time.Now()
	sb.FinishedAt = &finishTime
	result.ExecutionTime = finishTime.Sub(startTime)
	result.Stdout = string(stdoutData)
	result.Stderr = string(stderrData)

	sb.logger.Info("Sandbox execution finished",
		zap.Int("exit_code", result.ExitCode),
		zap.Duration("execution_time", result.ExecutionTime),
		zap.Int("stdout_size", len(stdoutData)),
		zap.Int("stderr_size", len(stderrData)),
	)

	return result, nil
}

func (m *Manager) DestroySandbox(sb *Sandbox) error {
	sb.mu.Lock()
	defer sb.mu.Unlock()

	if sb.Status == SandboxStatusDestroyed {
		return nil
	}

	if sb.cancelFunc != nil {
		sb.cancelFunc()
	}

	if sb.cmd != nil && sb.cmd.Process != nil {
		if err := sb.cmd.Process.Kill(); err != nil {
			sb.logger.Debug("Failed to kill sandbox process (may already be dead)", zap.Error(err))
		}
	}

	destroyCmd := exec.Command(m.cfg.Sandbox.RunscBinPath, "delete", "--force", sb.ID)
	if err := destroyCmd.Run(); err != nil {
		sb.logger.Warn("Failed to delete runsc sandbox", zap.Error(err))
	}

	if err := os.RemoveAll(sb.RootDir); err != nil {
		sb.logger.Error("Failed to remove sandbox directory",
			zap.String("root_dir", sb.RootDir),
			zap.Error(err),
		)
	}

	sb.Status = SandboxStatusDestroyed
	sb.logger.Info("Sandbox destroyed")

	m.mu.Lock()
	delete(m.sandboxes, sb.ID)
	m.mu.Unlock()

	select {
	case <-m.sem:
	default:
	}

	return nil
}

func (m *Manager) GetSandbox(id string) (*Sandbox, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	sb, ok := m.sandboxes[id]
	return sb, ok
}

func (m *Manager) ListSandboxes() []*Sandbox {
	m.mu.RLock()
	defer m.mu.RUnlock()
	sandboxes := make([]*Sandbox, 0, len(m.sandboxes))
	for _, sb := range m.sandboxes {
		sandboxes = append(sandboxes, sb)
	}
	return sandboxes
}

func (m *Manager) buildRunscArgs(sb *Sandbox, command string, args []string) []string {
	runscArgs := []string{
		"--root", "/var/run/runtime",
		"--debug",
		"--debug-log", filepath.Join(sb.RootDir, "runsc.log"),
		"run",
		"--bundle", sb.RootDir,
		"--detach=false",
	}

	if m.cfg.Resources.NetworkMode == "none" {
		runscArgs = append(runscArgs, "--network=none")
	}

	cpuLimit := fmt.Sprintf("%.0f", m.cfg.Resources.CPULimit*1000)
	memLimit := fmt.Sprintf("%d", m.cfg.Resources.MemoryLimit)
	pidLimit := fmt.Sprintf("%d", m.cfg.Resources.PIDLimit)

	runscArgs = append(runscArgs, "--cpu-quota="+cpuLimit)
	runscArgs = append(runscArgs, "--cpu-period=100000")
	runscArgs = append(runscArgs, "--memory-limit="+memLimit)
	runscArgs = append(runscArgs, "--pid-limit="+pidLimit)
	runscArgs = append(runscArgs, "--read-only")

	return append(runscArgs, command, args...)
}

func (m *Manager) readOutputs(stdoutPipe, stderrPipe *os.File) ([]byte, []byte) {
	var wg sync.WaitGroup
	var stdoutData, stderrData []byte
	var stdoutErr, stderrErr error

	wg.Add(2)

	go func() {
		defer wg.Done()
		stdoutData, stdoutErr = readAllWithLimit(stdoutPipe, 64*1024*1024)
	}()

	go func() {
		defer wg.Done()
		stderrData, stderrErr = readAllWithLimit(stderrPipe, 64*1024*1024)
	}()

	wg.Wait()

	if stdoutErr != nil {
		m.logger.Warn("Failed to read stdout", zap.Error(stdoutErr))
	}
	if stderrErr != nil {
		m.logger.Warn("Failed to read stderr", zap.Error(stderrErr))
	}

	return stdoutData, stderrData
}

func readAllWithLimit(r *os.File, limit int64) ([]byte, error) {
	buf := make([]byte, 32*1024)
	result := make([]byte, 0, 4096)
	var total int64

	for {
		if total >= limit {
			break
		}
		n, err := r.Read(buf)
		if n > 0 {
			remaining := limit - total
			toRead := int64(n)
			if toRead > remaining {
				toRead = remaining
			}
			result = append(result, buf[:toRead]...)
			total += toRead
		}
		if err != nil {
			if err == io.EOF {
				break
			}
			return result, err
		}
	}

	return result, nil
}

func (m *Manager) setOOMScoreAdj(pid int, score int) error {
	oomPath := fmt.Sprintf("/proc/%d/oom_score_adj", pid)
	content := fmt.Sprintf("%d", score)
	return os.WriteFile(oomPath, []byte(content), 0644)
}

func (m *Manager) failSandbox(sb *Sandbox, startTime time.Time) {
	sb.mu.Lock()
	sb.Status = SandboxStatusFailed
	finishTime := time.Now()
	sb.FinishedAt = &finishTime
	sb.mu.Unlock()
}

func (m *Manager) cleanupFailedSandbox(sb *Sandbox) {
	os.RemoveAll(sb.RootDir)
	select {
	case <-m.sem:
	default:
	}
}

func (s *Sandbox) GetStatus() SandboxStatus {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.Status
}

func (s *Sandbox) GetStats() *stats.ResourceStats {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.stats.Clone()
}
