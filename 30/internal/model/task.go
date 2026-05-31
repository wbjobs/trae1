package model

import (
	"context"
	"sync"
	"time"
)

type TaskStatus string

const (
	StatusPending    TaskStatus = "pending"
	StatusRunning    TaskStatus = "running"
	StatusPaused     TaskStatus = "paused"
	StatusStopped    TaskStatus = "stopped"
	StatusFailed     TaskStatus = "failed"
	StatusRecovering TaskStatus = "recovering"
	StatusZombie     TaskStatus = "zombie"
)

type InputProtocol string

const (
	ProtoRTMP InputProtocol = "rtmp"
	ProtoHLS  InputProtocol = "hls"
	ProtoSRT  InputProtocol = "srt"
)

type Priority int

const (
	PriorityLow    Priority = 1
	PriorityMedium Priority = 2
	PriorityHigh   Priority = 3
)

func ParsePriority(s string) (Priority, bool) {
	switch s {
	case "high", "High", "HIGH", "3":
		return PriorityHigh, true
	case "medium", "Medium", "MEDIUM", "2":
		return PriorityMedium, true
	case "low", "Low", "LOW", "1":
		return PriorityLow, true
	}
	return 0, false
}

func (p Priority) String() string {
	switch p {
	case PriorityHigh:
		return "high"
	case PriorityMedium:
		return "medium"
	case PriorityLow:
		return "low"
	}
	return "unknown"
}

type Task struct {
	ID          string        `json:"id"`
	Name        string        `json:"name"`
	InputURL    string        `json:"input_url"`
	Protocol    InputProtocol `json:"protocol"`
	Profile     string        `json:"profile"`
	Priority    Priority      `json:"priority"`
	OutputDir   string        `json:"output_dir"`
	Status      TaskStatus    `json:"status"`
	RetryCount  int           `json:"retry_count"`
	CreatedAt   time.Time     `json:"created_at"`
	StartedAt   time.Time     `json:"started_at,omitempty"`
	StoppedAt   time.Time     `json:"stopped_at,omitempty"`
	LastProbeAt time.Time     `json:"last_probe_at,omitempty"`
	LastError   string        `json:"last_error,omitempty"`
	PID         int           `json:"pid,omitempty"`

	mu     sync.Mutex
	stopCh chan struct{}
	doneCh chan struct{}
	cancel context.CancelFunc
}

type TaskRequest struct {
	Name     string        `json:"name" binding:"required"`
	InputURL string        `json:"input_url" binding:"required"`
	Protocol InputProtocol `json:"protocol" binding:"required"`
	Profile  string        `json:"profile" binding:"required"`
	Priority string        `json:"priority"`
}

type QueueSnapshot struct {
	HighPending   []string `json:"high_pending"`
	MediumPending []string `json:"medium_pending"`
	LowPending    []string `json:"low_pending"`
	Running       []string `json:"running"`
	Capacity      int      `json:"capacity"`
	RunningCount  int      `json:"running_count"`
}

func (t *Task) SetStatus(s TaskStatus) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.Status = s
}

func (t *Task) GetStatus() TaskStatus {
	t.mu.Lock()
	defer t.mu.Unlock()
	return t.Status
}

func (t *Task) MarkStopped(err string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.Status = StatusStopped
	t.StoppedAt = time.Now()
	t.LastError = err
}

func (t *Task) MarkFailed(err string) {
	t.mu.Lock()
	defer t.mu.Unlock()
	t.Status = StatusFailed
	t.StoppedAt = time.Now()
	t.LastError = err
}
