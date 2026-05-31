package aisecurity

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/http"
	"sync"
	"time"

	"go.uber.org/zap"
)

type ApprovalStatus string

const (
	ApprovalStatusPending  ApprovalStatus = "pending"
	ApprovalStatusApproved ApprovalStatus = "approved"
	ApprovalStatusRejected ApprovalStatus = "rejected"
)

type PendingApproval struct {
	RequestID  string              `json:"request_id"`
	Command    string              `json:"command"`
	Args       []string            `json:"args"`
	Score      int                 `json:"score"`
	Reasons    []string            `json:"reasons"`
	Category   string              `json:"category"`
	Status     ApprovalStatus      `json:"status"`
	CreatedAt  time.Time           `json:"created_at"`
	ExpiresAt  time.Time           `json:"expires_at"`
	ResolvedAt *time.Time          `json:"resolved_at,omitempty"`
	done       chan struct{}
}

type WebhookPayload struct {
	Event     string          `json:"event"`
	RequestID string          `json:"request_id"`
	Command   string          `json:"command"`
	Args      []string        `json:"args"`
	Score     int             `json:"score"`
	Reasons   []string        `json:"reasons"`
	Category  string          `json:"category"`
	Timestamp time.Time       `json:"timestamp"`
}

type ApprovalManager struct {
	webhookURL string
	logger     *zap.Logger
	pending    map[string]*PendingApproval
	mu         sync.RWMutex
	ttl        time.Duration
	httpClient *http.Client
}

func NewApprovalManager(webhookURL string, ttl time.Duration, logger *zap.Logger) *ApprovalManager {
	return &ApprovalManager{
		webhookURL: webhookURL,
		logger:     logger,
		pending:    make(map[string]*PendingApproval),
		ttl:        ttl,
		httpClient: &http.Client{
			Timeout: 10 * time.Second,
		},
	}
}

func (m *ApprovalManager) RequestApproval(requestID, command string, args []string, assessment *SecurityAssessment) (*PendingApproval, error) {
	approval := &PendingApproval{
		RequestID: requestID,
		Command:   command,
		Args:      args,
		Score:     assessment.Score,
		Reasons:   assessment.Reasons,
		Category:  assessment.Category,
		Status:    ApprovalStatusPending,
		CreatedAt: time.Now(),
		ExpiresAt: time.Now().Add(m.ttl),
		done:      make(chan struct{}),
	}

	m.mu.Lock()
	m.pending[requestID] = approval
	m.mu.Unlock()

	if m.webhookURL != "" {
		if err := m.sendWebhook(approval); err != nil {
			m.logger.Warn("Failed to send approval webhook",
				zap.String("request_id", requestID),
				zap.Error(err),
			)
		}
	}

	m.logger.Info("Manual approval requested",
		zap.String("request_id", requestID),
		zap.String("command", command),
		zap.Int("score", assessment.Score),
		zap.Strings("reasons", assessment.Reasons),
	)

	return approval, nil
}

func (m *ApprovalManager) ResolveApproval(requestID string, approved bool, reason string) error {
	m.mu.Lock()
	approval, ok := m.pending[requestID]
	m.mu.Unlock()

	if !ok {
		return fmt.Errorf("approval request not found: %s", requestID)
	}

	if approval.Status != ApprovalStatusPending {
		return fmt.Errorf("approval request already resolved: %s", requestID)
	}

	now := time.Now()
	approval.ResolvedAt = &now

	if approved {
		approval.Status = ApprovalStatusApproved
		m.logger.Info("Approval granted",
			zap.String("request_id", requestID),
			zap.String("reason", reason),
		)
	} else {
		approval.Status = ApprovalStatusRejected
		m.logger.Info("Approval rejected",
			zap.String("request_id", requestID),
			zap.String("reason", reason),
		)
	}

	close(approval.done)
	return nil
}

func (m *ApprovalManager) GetPending(requestID string) (*PendingApproval, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()

	approval, ok := m.pending[requestID]
	return approval, ok
}

func (m *ApprovalManager) ListPending() []*PendingApproval {
	m.mu.RLock()
	defer m.mu.RUnlock()

	result := make([]*PendingApproval, 0, len(m.pending))
	now := time.Now()
	for _, approval := range m.pending {
		if approval.Status == ApprovalStatusPending && now.Before(approval.ExpiresAt) {
			result = append(result, approval)
		}
	}
	return result
}

func (m *ApprovalManager) sendWebhook(approval *PendingApproval) error {
	payload := WebhookPayload{
		Event:     "approval_requested",
		RequestID: approval.RequestID,
		Command:   approval.Command,
		Args:      approval.Args,
		Score:     approval.Score,
		Reasons:   approval.Reasons,
		Category:  approval.Category,
		Timestamp: time.Now(),
	}

	jsonBody, err := json.Marshal(payload)
	if err != nil {
		return fmt.Errorf("failed to marshal webhook payload: %w", err)
	}

	req, err := http.NewRequest("POST", m.webhookURL, bytes.NewReader(jsonBody))
	if err != nil {
		return fmt.Errorf("failed to create webhook request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := m.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("webhook request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 300 {
		return fmt.Errorf("webhook returned status %d", resp.StatusCode)
	}

	return nil
}

func (m *ApprovalManager) cleanupExpired() {
	m.mu.Lock()
	defer m.mu.Unlock()

	now := time.Now()
	for id, approval := range m.pending {
		if now.After(approval.ExpiresAt) && approval.Status == ApprovalStatusPending {
			approval.Status = ApprovalStatusRejected
			close(approval.done)
			delete(m.pending, id)
			m.logger.Info("Expired pending approval cleaned up",
				zap.String("request_id", id),
			)
		}
	}
}

func (m *ApprovalManager) StartCleanupLoop() {
	ticker := time.NewTicker(1 * time.Minute)
	go func() {
		for range ticker.C {
			m.cleanupExpired()
		}
	}()
}
