package aisecurity

import (
	"context"
	"fmt"
	"time"

	"go.uber.org/zap"

	"github.com/sandbox/executor/internal/config"
)

type CheckDecision string

const (
	DecisionReject   CheckDecision = "reject"
	DecisionApprove  CheckDecision = "approve"
	DecisionReview   CheckDecision = "review"
)

type CheckResult struct {
	Decision   CheckDecision       `json:"decision"`
	Score      int                 `json:"score"`
	Reasons    []string            `json:"reasons"`
	Category   string              `json:"category"`
	Cached     bool                `json:"cached"`
	ApprovalID string              `json:"approval_id,omitempty"`
}

type SecurityChecker struct {
	ollama    *OllamaClient
	cache     *Cache
	approval  *ApprovalManager
	logger    *zap.Logger
	cfg       *config.AISecurityConfig
}

func NewSecurityChecker(cfg *config.AISecurityConfig, logger *zap.Logger) *SecurityChecker {
	var ollamaClient *OllamaClient
	if cfg.Enabled {
		ollamaClient = NewOllamaClient(cfg.OllamaURL, cfg.Model, cfg.RequestTimeout)
	}

	cache := NewCache(cfg.CacheTTL)

	var approvalMgr *ApprovalManager
	if cfg.ApprovalWebhookURL != "" {
		approvalMgr = NewApprovalManager(cfg.ApprovalWebhookURL, cfg.ApprovalTimeout, logger)
		approvalMgr.StartCleanupLoop()
	} else {
		approvalMgr = NewApprovalManager("", cfg.ApprovalTimeout, logger)
		approvalMgr.StartCleanupLoop()
	}

	return &SecurityChecker{
		ollama:   ollamaClient,
		cache:    cache,
		approval: approvalMgr,
		logger:   logger,
		cfg:      cfg,
	}
}

func (c *SecurityChecker) PreCheck(ctx context.Context, requestID, command string, args []string) (*CheckResult, error) {
	if !c.cfg.Enabled {
		return &CheckResult{
			Decision: DecisionApprove,
			Score:    100,
			Reasons:  []string{"AI security pre-check is disabled"},
			Category: "safe",
		}, nil
	}

	if cached, ok := c.cache.Get(command, args); ok {
		c.logger.Debug("Cache hit for AI pre-check",
			zap.String("command", command),
			zap.Int("score", cached.Score),
		)
		return c.makeDecision(requestID, command, args, cached, true)
	}

	assessment, err := c.ollama.Check(ctx, command, args)
	if err != nil {
		c.logger.Warn("AI pre-check failed, using fallback",
			zap.String("command", command),
			zap.Error(err),
		)
		assessment = &SecurityAssessment{
			Score:    60,
			Reasons:  []string{fmt.Sprintf("AI check failed: %v, requiring manual review", err)},
			Category: "suspicious",
		}
	}

	c.cache.Set(command, args, assessment)

	c.logger.Info("AI pre-check completed",
		zap.String("command", command),
		zap.Int("score", assessment.Score),
		zap.String("category", assessment.Category),
		zap.Strings("reasons", assessment.Reasons),
	)

	return c.makeDecision(requestID, command, args, assessment, false)
}

func (c *SecurityChecker) makeDecision(requestID, command string, args []string, assessment *SecurityAssessment, cached bool) (*CheckResult, error) {
	result := &CheckResult{
		Score:    assessment.Score,
		Reasons:  assessment.Reasons,
		Category: assessment.Category,
		Cached:   cached,
	}

	switch {
	case assessment.Score >= c.cfg.ApproveThreshold:
		result.Decision = DecisionApprove
		c.logger.Debug("AI pre-check: auto-approved",
			zap.Int("score", assessment.Score),
			zap.Int("threshold", c.cfg.ApproveThreshold),
		)

	case assessment.Score >= c.cfg.RejectThreshold:
		result.Decision = DecisionReview
		approval, err := c.approval.RequestApproval(requestID, command, args, assessment)
		if err != nil {
			return nil, fmt.Errorf("failed to request approval: %w", err)
		}
		result.ApprovalID = approval.RequestID

		c.logger.Info("AI pre-check: manual review required",
			zap.String("request_id", requestID),
			zap.Int("score", assessment.Score),
			zap.Int("reject_threshold", c.cfg.RejectThreshold),
			zap.Int("approve_threshold", c.cfg.ApproveThreshold),
		)

	default:
		result.Decision = DecisionReject
		c.logger.Warn("AI pre-check: rejected",
			zap.Int("score", assessment.Score),
			zap.Int("reject_threshold", c.cfg.RejectThreshold),
			zap.Strings("reasons", assessment.Reasons),
		)
	}

	return result, nil
}

func (c *SecurityChecker) ResolveApproval(requestID string, approved bool, reason string) error {
	return c.approval.ResolveApproval(requestID, approved, reason)
}

func (c *SecurityChecker) GetPendingApproval(requestID string) (*PendingApproval, bool) {
	return c.approval.GetPending(requestID)
}

func (c *SecurityChecker) ListPendingApprovals() []*PendingApproval {
	return c.approval.ListPending()
}

func (c *SecurityChecker) WaitForApproval(ctx context.Context, requestID string) (bool, error) {
	approval, ok := c.approval.GetPending(requestID)
	if !ok {
		return false, fmt.Errorf("pending approval not found: %s", requestID)
	}

	select {
	case <-approval.done:
		return approval.Status == ApprovalStatusApproved, nil
	case <-ctx.Done():
		return false, ctx.Err()
	case <-time.After(c.cfg.ApprovalTimeout):
		return false, fmt.Errorf("approval timeout for request: %s", requestID)
	}
}

func (c *SecurityChecker) ClearCache() {
	c.cache.Clear()
}
