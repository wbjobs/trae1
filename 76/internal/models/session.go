package models

import (
	"sync"
	"time"

	"github.com/rs/xid"
)

type SessionStatus string

const (
	SessionStatusRunning   SessionStatus = "running"
	SessionStatusCompleted SessionStatus = "completed"
	SessionStatusFailed    SessionStatus = "failed"
)

type RiskLevel string

const (
	RiskLevelLow      RiskLevel = "low"
	RiskLevelMedium   RiskLevel = "medium"
	RiskLevelHigh     RiskLevel = "high"
	RiskLevelCritical RiskLevel = "critical"
)

type Session struct {
	ID            string        `json:"id"`
	User          string        `json:"user"`
	TargetHost    string        `json:"target_host"`
	TargetPort    int           `json:"target_port"`
	TargetUser    string        `json:"target_user"`
	Status        SessionStatus `json:"status"`
	StartTime     time.Time     `json:"start_time"`
	EndTime       *time.Time    `json:"end_time,omitempty"`
	Duration      float64       `json:"duration"`
	RecordFile    string        `json:"record_file"`
	ObjectKey     string        `json:"object_key"`
	ClientIP      string        `json:"client_ip"`
	Commands      []CommandEntry `json:"commands,omitempty"`
	RiskLevel     RiskLevel     `json:"risk_level"`
	RiskFindings  []RiskFinding `json:"risk_findings,omitempty"`
	Width         int           `json:"width"`
	Height        int           `json:"height"`
	FrameCount    int           `json:"frame_count"`
	AIResults     []AIRiskResult `json:"ai_results,omitempty"`
	ApprovalRecords []ApprovalRecordItem `json:"approval_records,omitempty"`
	LiveStream    *LiveStream   `json:"-"`
	FrameStore    interface{}   `json:"-"`
	mu            sync.RWMutex  `json:"-"`
}

type AIRiskResult struct {
	Command    string `json:"command"`
	RiskLevel  int    `json:"risk_level"`
	Intent     string `json:"intent"`
	Category   string `json:"category"`
	RiskReason string `json:"risk_reason"`
	AnalyzedAt time.Time `json:"analyzed_at"`
	Model      string `json:"model"`
}

type ApprovalRecordItem struct {
	Command           string    `json:"command"`
	RiskLevel         int       `json:"risk_level"`
	Intent            string    `json:"intent"`
	Category          string    `json:"category"`
	RiskReason        string    `json:"risk_reason"`
	ProcessInstanceID string    `json:"process_instance_id"`
	CreatedAt         time.Time `json:"created_at"`
	ResolvedAt        time.Time `json:"resolved_at,omitempty"`
	Status            string    `json:"status"`
	ApproverUserID    string    `json:"approver_user_id,omitempty"`
	ApproverRemark    string    `json:"approver_remark,omitempty"`
}

type CommandEntry struct {
	Timestamp  float64 `json:"timestamp"`
	Command    string  `json:"command"`
}

type RiskFinding struct {
	Command   string    `json:"command"`
	Pattern   string    `json:"pattern"`
	Timestamp float64   `json:"timestamp"`
	Severity  RiskLevel `json:"severity"`
}

type LiveStream struct {
	Subscribers map[chan []byte]struct{}
	mu          sync.RWMutex
	done        chan struct{}
	closed      bool
}

func NewSession(user, targetHost string, targetPort int, targetUser, clientIP string) *Session {
	return &Session{
		ID:         xid.New().String(),
		User:       user,
		TargetHost: targetHost,
		TargetPort: targetPort,
		TargetUser: targetUser,
		Status:     SessionStatusRunning,
		StartTime:  time.Now(),
		ClientIP:   clientIP,
		Commands:   make([]CommandEntry, 0),
		RiskLevel:  RiskLevelLow,
		Width:      80,
		Height:     24,
		LiveStream: NewLiveStream(),
	}
}

func NewLiveStream() *LiveStream {
	return &LiveStream{
		Subscribers: make(map[chan []byte]struct{}),
		done:        make(chan struct{}),
	}
}

func (ls *LiveStream) Subscribe() (chan []byte, chan struct{}) {
	ls.mu.Lock()
	defer ls.mu.Unlock()
	ch := make(chan []byte, 256)
	ls.Subscribers[ch] = struct{}{}
	return ch, ls.done
}

func (ls *LiveStream) Unsubscribe(ch chan []byte) {
	ls.mu.Lock()
	defer ls.mu.Unlock()
	delete(ls.Subscribers, ch)
}

func (ls *LiveStream) Broadcast(data []byte) {
	ls.mu.RLock()
	defer ls.mu.RUnlock()
	if ls.closed {
		return
	}
	buf := make([]byte, len(data))
	copy(buf, data)
	for ch := range ls.Subscribers {
		select {
		case ch <- buf:
		default:
		}
	}
}

func (ls *LiveStream) Close() {
	ls.mu.Lock()
	defer ls.mu.Unlock()
	if !ls.closed {
		ls.closed = true
		close(ls.done)
		ls.Subscribers = make(map[chan []byte]struct{})
	}
}

func (s *Session) AddCommand(timestamp float64, cmd string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.Commands = append(s.Commands, CommandEntry{
		Timestamp: timestamp,
		Command:   cmd,
	})
}

func (s *Session) GetCommands() []CommandEntry {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]CommandEntry, len(s.Commands))
	copy(result, s.Commands)
	return result
}

func (s *Session) Complete(recordFile, objectKey string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	s.EndTime = &now
	s.Duration = now.Sub(s.StartTime).Seconds()
	s.RecordFile = recordFile
	s.ObjectKey = objectKey
	s.Status = SessionStatusCompleted
	if fs, ok := s.FrameStore.(interface{ Len() int }); ok {
		s.FrameCount = fs.Len()
	}
	if s.LiveStream != nil {
		s.LiveStream.Close()
	}
}

func (s *Session) Fail() {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	s.EndTime = &now
	s.Duration = now.Sub(s.StartTime).Seconds()
	s.Status = SessionStatusFailed
	if s.LiveStream != nil {
		s.LiveStream.Close()
	}
}

func (s *Session) SetRisk(level RiskLevel, findings []RiskFinding) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.RiskLevel = level
	s.RiskFindings = findings
}

func (s *Session) AddAIResult(result AIRiskResult) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.AIResults = append(s.AIResults, result)
}

func (s *Session) GetAIResults() []AIRiskResult {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]AIRiskResult, len(s.AIResults))
	copy(result, s.AIResults)
	return result
}

func (s *Session) AddApprovalRecord(record ApprovalRecordItem) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.ApprovalRecords = append(s.ApprovalRecords, record)
}

func (s *Session) GetApprovalRecords() []ApprovalRecordItem {
	s.mu.RLock()
	defer s.mu.RUnlock()
	result := make([]ApprovalRecordItem, len(s.ApprovalRecords))
	copy(result, s.ApprovalRecords)
	return result
}

func (s *Session) GetHighestRiskLevel() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	highest := 1
	for _, r := range s.AIResults {
		if r.RiskLevel > highest {
			highest = r.RiskLevel
		}
	}
	return highest
}

func (s *Session) GetBlockedCount() int {
	s.mu.RLock()
	defer s.mu.RUnlock()
	count := 0
	for _, r := range s.ApprovalRecords {
		if r.Status == "rejected" || r.Status == "timeout" {
			count++
		}
	}
	return count
}

type SessionStore struct {
	sessions map[string]*Session
	mu       sync.RWMutex
}

func NewSessionStore() *SessionStore {
	return &SessionStore{
		sessions: make(map[string]*Session),
	}
}

func (ss *SessionStore) Add(s *Session) {
	ss.mu.Lock()
	defer ss.mu.Unlock()
	ss.sessions[s.ID] = s
}

func (ss *SessionStore) Get(id string) (*Session, bool) {
	ss.mu.RLock()
	defer ss.mu.RUnlock()
	s, ok := ss.sessions[id]
	return s, ok
}

func (ss *SessionStore) List() []*Session {
	ss.mu.RLock()
	defer ss.mu.RUnlock()
	result := make([]*Session, 0, len(ss.sessions))
	for _, s := range ss.sessions {
		result = append(result, s)
	}
	return result
}

func (ss *SessionStore) Update(id string, fn func(*Session)) {
	ss.mu.Lock()
	defer ss.mu.Unlock()
	if s, ok := ss.sessions[id]; ok {
		fn(s)
	}
}
