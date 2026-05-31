package audit

import (
	"context"
	"encoding/json"
	"sync"
	"time"

	"github.com/spiffe-gateway/svid-gateway/internal/policy"
)

type Appender interface {
	AppendAuditJSON(ctx context.Context, raw []byte) error
}

type Entry struct {
	Time     time.Time      `json:"time"`
	Action   string         `json:"action"`
	Operator string         `json:"operator"`
	PolicyID string         `json:"policy_id,omitempty"`
	Before   *policy.Policy `json:"before,omitempty"`
	After    *policy.Policy `json:"after,omitempty"`
	Message  string         `json:"message,omitempty"`
}

type Logger struct {
	appender Appender
	buf      []Entry
	mu       sync.Mutex
}

func NewLogger(a Appender) *Logger {
	return &Logger{appender: a}
}

func (l *Logger) Log(ctx context.Context, e Entry) {
	if e.Time.IsZero() {
		e.Time = time.Now().UTC()
	}
	l.mu.Lock()
	l.buf = append(l.buf, e)
	l.mu.Unlock()

	if l.appender != nil {
		raw, _ := json.Marshal(e)
		_ = l.appender.AppendAuditJSON(ctx, raw)
	}
}

func (l *Logger) Recent(n int) []Entry {
	l.mu.Lock()
	defer l.mu.Unlock()
	if n <= 0 || n > len(l.buf) {
		n = len(l.buf)
	}
	out := make([]Entry, n)
	copy(out, l.buf[len(l.buf)-n:])
	return out
}
