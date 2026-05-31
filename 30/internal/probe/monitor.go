package probe

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/url"
	"strings"
	"sync"
	"time"

	"transcode-gateway/internal/config"
	"transcode-gateway/internal/logger"
	"transcode-gateway/internal/model"
)

type StreamHealth struct {
	LastSuccess time.Time `json:"last_success"`
	LastFail    time.Time `json:"last_fail"`
	ErrMsg      string    `json:"err_msg,omitempty"`
	Healthy     bool      `json:"healthy"`
}

type Monitor struct {
	cfg    *config.Config
	log    *logger.Logger
	states map[string]*StreamHealth
	mu     sync.RWMutex
	http   *http.Client
}

func New(cfg *config.Config, log *logger.Logger) *Monitor {
	timeout := time.Duration(cfg.StreamMonitor.ProbeTimeout) * time.Second
	return &Monitor{
		cfg:    cfg,
		log:    log,
		states: make(map[string]*StreamHealth),
		http:   &http.Client{Timeout: timeout},
	}
}

func (m *Monitor) Probe(ctx context.Context, task *model.Task) error {
	if !m.cfg.StreamMonitor.Enabled {
		now := time.Now()
		m.update(task.ID, now, nil)
		return nil
	}

	var err error
	switch task.Protocol {
	case model.ProtoHLS:
		err = m.probeHLS(ctx, task.InputURL)
	case model.ProtoRTMP:
		err = m.probeTCP(ctx, task.InputURL, 1935)
	case model.ProtoSRT:
		err = m.probeUDP(ctx, task.InputURL)
	default:
		err = fmt.Errorf("未知协议: %s", task.Protocol)
	}

	now := time.Now()
	m.update(task.ID, now, err)
	return err
}

func (m *Monitor) update(id string, t time.Time, err error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	s, ok := m.states[id]
	if !ok {
		s = &StreamHealth{}
		m.states[id] = s
	}
	if err == nil {
		s.LastSuccess = t
		s.Healthy = true
		s.ErrMsg = ""
	} else {
		s.LastFail = t
		s.Healthy = false
		s.ErrMsg = err.Error()
	}
}

func (m *Monitor) State(id string) *StreamHealth {
	m.mu.RLock()
	defer m.mu.RUnlock()
	if s, ok := m.states[id]; ok {
		cp := *s
		return &cp
	}
	return nil
}

func (m *Monitor) Disconnected(id string) bool {
	m.mu.RLock()
	defer m.mu.RUnlock()
	s, ok := m.states[id]
	if !ok {
		return false
	}
	if s.LastSuccess.IsZero() {
		return false
	}
	return time.Since(s.LastSuccess) > time.Duration(m.cfg.StreamMonitor.DisconnectTimeout)*time.Second
}

func (m *Monitor) Remove(id string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.states, id)
}

func (m *Monitor) probeHLS(ctx context.Context, inputURL string) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, inputURL, nil)
	if err != nil {
		return err
	}
	resp, err := m.http.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 400 {
		return fmt.Errorf("HLS 探测失败 HTTP %d", resp.StatusCode)
	}
	return nil
}

func (m *Monitor) probeTCP(ctx context.Context, inputURL string, defaultPort int) error {
	host, err := extractHost(inputURL, defaultPort)
	if err != nil {
		return err
	}
	var d net.Dialer
	conn, err := d.DialContext(ctx, "tcp", host)
	if err != nil {
		return fmt.Errorf("RTMP 端口不可达: %w", err)
	}
	_ = conn.Close()
	return nil
}

func (m *Monitor) probeUDP(ctx context.Context, inputURL string) error {
	host, err := extractHost(inputURL, 9000)
	if err != nil {
		return err
	}
	var d net.Dialer
	conn, err := d.DialContext(ctx, "udp", host)
	if err != nil {
		return fmt.Errorf("SRT 端口不可达: %w", err)
	}
	_ = conn.Close()
	return nil
}

func extractHost(inputURL string, defaultPort int) (string, error) {
	s := strings.Replace(inputURL, "srt://", "tcp://", 1)
	u, err := url.Parse(s)
	if err != nil {
		return "", fmt.Errorf("解析 URL 失败: %w", err)
	}
	host := u.Host
	if !strings.Contains(host, ":") {
		host = fmt.Sprintf("%s:%d", host, defaultPort)
	}
	return host, nil
}
