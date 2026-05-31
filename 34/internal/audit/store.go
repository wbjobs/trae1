package audit

import (
	"sort"
	"sync"
	"time"
)

type MessageEvent struct {
	TraceID     string            `json:"trace_id"`
	SpanID      string            `json:"span_id"`
	ParentSpanID string           `json:"parent_span_id,omitempty"`
	Source      string            `json:"source"`
	SourceTopic string            `json:"source_topic"`
	Target      string            `json:"target"`
	TargetTopic string            `json:"target_topic"`
	Format      string            `json:"format"`
	Status      string            `json:"status"`
	Error       string            `json:"error,omitempty"`
	DurationMs  int64             `json:"duration_ms"`
	Timestamp   time.Time         `json:"timestamp"`
	Headers     map[string]string `json:"headers,omitempty"`
	PayloadSize int               `json:"payload_size"`
}

type Lifecycle struct {
	TraceID  string          `json:"trace_id"`
	Events   []*MessageEvent `json:"events"`
	Status   string          `json:"status"`
	CreatedAt time.Time      `json:"created_at"`
	UpdatedAt time.Time      `json:"updated_at"`
}

type Store struct {
	mu        sync.RWMutex
	lifecycles map[string]*Lifecycle
	maxSize   int
}

func NewStore(maxSize int) *Store {
	if maxSize <= 0 {
		maxSize = 10000
	}
	return &Store{
		lifecycles: make(map[string]*Lifecycle),
		maxSize:    maxSize,
	}
}

func (s *Store) Record(event *MessageEvent) {
	if event.TraceID == "" {
		return
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	lc, ok := s.lifecycles[event.TraceID]
	if !ok {
		if len(s.lifecycles) >= s.maxSize {
			s.evictOldest()
		}
		lc = &Lifecycle{
			TraceID:   event.TraceID,
			Events:    make([]*MessageEvent, 0),
			CreatedAt: time.Now(),
		}
		s.lifecycles[event.TraceID] = lc
	}

	lc.Events = append(lc.Events, event)
	lc.UpdatedAt = time.Now()

	if event.Status == "error" || event.Status == "dlq" {
		lc.Status = event.Status
	} else if lc.Status == "" && event.Status == "success" {
		lc.Status = "success"
	}
}

func (s *Store) evictOldest() {
	var oldestKey string
	var oldestTime time.Time
	first := true
	for k, v := range s.lifecycles {
		if first || v.UpdatedAt.Before(oldestTime) {
			oldestKey = k
			oldestTime = v.UpdatedAt
			first = false
		}
	}
	if oldestKey != "" {
		delete(s.lifecycles, oldestKey)
	}
}

func (s *Store) Get(traceID string) (*Lifecycle, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	lc, ok := s.lifecycles[traceID]
	return lc, ok
}

func (s *Store) List(limit, offset int) []*Lifecycle {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var lcs []*Lifecycle
	for _, lc := range s.lifecycles {
		lcs = append(lcs, lc)
	}

	sort.Slice(lcs, func(i, j int) bool {
		return lcs[i].UpdatedAt.After(lcs[j].UpdatedAt)
	})

	if offset >= len(lcs) {
		return nil
	}
	end := offset + limit
	if end > len(lcs) {
		end = len(lcs)
	}
	return lcs[offset:end]
}

func (s *Store) Size() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return len(s.lifecycles)
}
