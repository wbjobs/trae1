package transcoder

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"sync"
	"syscall"
	"time"

	"github.com/sirupsen/logrus"

	"transcode-gateway/internal/config"
	"transcode-gateway/internal/logger"
	"transcode-gateway/internal/model"

	ffmpeg_g "github.com/u2takey/ffmpeg-go"
)

type FFmpegHandle struct {
	taskID    string
	cmd       *exec.Cmd
	cancel    context.CancelFunc
	mu        sync.Mutex
	stopped   bool
	startTime time.Time
}

type TranscodeResult struct {
	Err       error
	Retriable bool
}

type Manager struct {
	cfg    *config.Config
	log    *logger.Logger
	active map[string]*FFmpegHandle
	mu     sync.Mutex
}

func NewManager(cfg *config.Config, log *logger.Logger) *Manager {
	return &Manager{
		cfg:    cfg,
		log:    log,
		active: make(map[string]*FFmpegHandle),
	}
}

func (m *Manager) Start(ctx context.Context, task *model.Task) (<-chan TranscodeResult, error) {
	prof, ok := m.cfg.Profile(task.Profile)
	if !ok {
		return nil, fmt.Errorf("未知转码档位: %s", task.Profile)
	}

	outDir := filepath.Join(m.cfg.HLSOutput.Root, task.ID)
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return nil, fmt.Errorf("创建输出目录失败: %w", err)
	}
	task.OutputDir = outDir

	segFile := filepath.Join(outDir, "seg%03d.ts")
	playlistFile := filepath.Join(outDir, "index.m3u8")

	procCtx, cancel := context.WithCancel(ctx)

	inputArgs := ffmpeg_g.KwArgs{}
	if m.cfg.StreamMonitor.MaxKeepalive {
		if task.Protocol == model.ProtoRTMP || task.Protocol == model.ProtoHLS {
			inputArgs["rw_timeout"] = "10000000"
		}
	}

	stream := ffmpeg_g.Input(task.InputURL, inputArgs)

	outputArgs := ffmpeg_g.KwArgs{
		"vcodec":              "libx264",
		"acodec":              "aac",
		"s":                   prof.Resolution,
		"b:v":                 prof.VideoBitrate,
		"b:a":                 prof.AudioBitrate,
		"preset":              prof.Preset,
		"f":                   "hls",
		"hls_time":            m.cfg.HLSOutput.SegmentDuration,
		"hls_list_size":       m.cfg.HLSOutput.SegmentCount,
		"hls_flags":           "delete_segments+append_list",
		"hls_segment_filename": segFile,
		"y":                   "",
	}

	out := stream.Output(playlistFile, outputArgs)

	cmd := out.OverWriteOutput().WithContext(procCtx).Compile()
	ffmpegBinary := m.cfg.FFmpeg.Binary
	if ffmpegBinary == "" {
		ffmpegBinary = "ffmpeg"
	}
	if p, err := exec.LookPath(ffmpegBinary); err == nil {
		cmd.Path = p
	} else {
		cmd.Path = ffmpegBinary
	}

	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid: true,
	}
	cmd.Stdout = nil
	cmd.Stderr = newLogWriter(m.log, task.ID, "ffmpeg")

	if err := cmd.Start(); err != nil {
		cancel()
		return nil, fmt.Errorf("启动 ffmpeg 失败: %w", err)
	}

	handle := &FFmpegHandle{
		taskID:    task.ID,
		cmd:       cmd,
		cancel:    cancel,
		startTime: time.Now(),
	}
	task.PID = cmd.Process.Pid
	m.mu.Lock()
	m.active[task.ID] = handle
	m.mu.Unlock()

	resCh := make(chan TranscodeResult, 1)
	go func() {
		err := cmd.Wait()
		m.mu.Lock()
		delete(m.active, task.ID)
		m.mu.Unlock()
		handle.mu.Lock()
		stopped := handle.stopped
		handle.mu.Unlock()
		cancel()
		if err != nil {
			if stopped {
				resCh <- TranscodeResult{Err: err, Retriable: false}
			} else {
				resCh <- TranscodeResult{Err: err, Retriable: true}
			}
		} else {
			resCh <- TranscodeResult{Err: nil, Retriable: false}
		}
	}()

	m.log.WithFields(logrus.Fields{
		"task_id": task.ID,
		"pid":     cmd.Process.Pid,
		"input":   task.InputURL,
		"output":  outDir,
	}).Info("转码进程已启动")

	return resCh, nil
}

func (m *Manager) Stop(taskID string) error {
	m.mu.Lock()
	handle, ok := m.active[taskID]
	m.mu.Unlock()
	if !ok {
		return nil
	}

	handle.mu.Lock()
	handle.stopped = true
	handle.mu.Unlock()

	graceful := time.Duration(m.cfg.FFmpeg.GracefulShutdownTimeout) * time.Second
	done := make(chan struct{})
	go func() {
		handle.cancel()
		if handle.cmd != nil && handle.cmd.Process != nil {
			_ = handle.cmd.Process.Signal(syscall.SIGTERM)
		}
		close(done)
	}()

	select {
	case <-done:
	case <-time.After(graceful):
		m.log.Warnf("任务 %s ffmpeg 优雅关闭超时，强制 kill", taskID)
		m.forceKill(handle)
	}
	return nil
}

func (m *Manager) Kill(taskID string) error {
	m.mu.Lock()
	handle, ok := m.active[taskID]
	m.mu.Unlock()
	if !ok {
		return nil
	}
	handle.mu.Lock()
	handle.stopped = true
	handle.mu.Unlock()
	m.forceKill(handle)
	return nil
}

func (m *Manager) forceKill(handle *FFmpegHandle) {
	if handle.cmd == nil || handle.cmd.Process == nil {
		return
	}
	pid := handle.cmd.Process.Pid
	proc, err := os.FindProcess(pid)
	if err != nil {
		return
	}
	if err := proc.Kill(); err != nil {
		m.log.Errorf("强制 kill 进程 %d 失败: %v", pid, err)
	} else {
		m.log.Warnf("已强制 kill 进程 pid=%d (task=%s)", pid, handle.taskID)
	}
}

func (m *Manager) IsRunning(taskID string) bool {
	m.mu.Lock()
	h, ok := m.active[taskID]
	m.mu.Unlock()
	if !ok {
		return false
	}
	if h.cmd == nil || h.cmd.Process == nil {
		return false
	}
	proc, err := os.FindProcess(h.cmd.Process.Pid)
	if err != nil {
		return false
	}
	return proc.Signal(syscall.Signal(0)) == nil
}

func (m *Manager) CleanupZombies() int {
	m.mu.Lock()
	handles := make([]*FFmpegHandle, 0, len(m.active))
	for _, h := range m.active {
		handles = append(handles, h)
	}
	m.mu.Unlock()

	cleaned := 0
	for _, h := range handles {
		if h.cmd == nil || h.cmd.Process == nil {
			continue
		}
		proc, err := os.FindProcess(h.cmd.Process.Pid)
		if err != nil {
			continue
		}
		if err := proc.Signal(syscall.Signal(0)); err != nil {
			m.mu.Lock()
			if cur, ok := m.active[h.taskID]; ok && cur == h {
				delete(m.active, h.taskID)
				cleaned++
			}
			m.mu.Unlock()
		}
	}
	return cleaned
}

type logWriter struct {
	log     *logger.Logger
	taskID  string
	section string
}

func newLogWriter(log *logger.Logger, taskID, section string) *logWriter {
	return &logWriter{log: log, taskID: taskID, section: section}
}

func (w *logWriter) Write(p []byte) (n int, err error) {
	w.log.Infof("[%s:%s] %s", w.taskID, w.section, string(p))
	return len(p), nil
}
