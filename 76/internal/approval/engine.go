package approval

import (
	"context"
	"fmt"
	"sync"
	"time"

	"bastion/internal/ai"
	"bastion/internal/config"
	"bastion/internal/dingtalk"
	"bastion/internal/models"
)

type Engine struct {
	cfg        *config.Config
	aiClient   *ai.OllamaClient
	dingClient *dingtalk.Client
	sessions   map[string]*PendingApproval
	mu         sync.RWMutex
}

type PendingApproval struct {
	SessionID         string
	Command           string
	AIResult          *ai.RiskAnalysisResult
	ProcessInstanceID string
	CreatedAt         time.Time
	ExpiresAt         time.Time
	Approved          bool
	Rejected          bool
	TimedOut          bool
	Done              chan struct{}
}

type ApprovalRecord struct {
	SessionID         string               `json:"session_id"`
	Command           string               `json:"command"`
	RiskLevel         int                  `json:"risk_level"`
	Intent            string               `json:"intent"`
	Category          string               `json:"category"`
	RiskReason        string               `json:"risk_reason"`
	AIResult          *ai.RiskAnalysisResult `json:"ai_result,omitempty"`
	ProcessInstanceID string               `json:"process_instance_id"`
	CreatedAt         time.Time            `json:"created_at"`
	ResolvedAt        time.Time            `json:"resolved_at,omitempty"`
	Status            string               `json:"status"`
	ApproverUserID    string               `json:"approver_user_id,omitempty"`
	ApproverRemark    string               `json:"approver_remark,omitempty"`
}

const (
	StatusPending  = "pending"
	StatusApproved = "approved"
	StatusRejected = "rejected"
	StatusTimeout  = "timeout"
)

func NewEngine(cfg *config.Config, aiClient *ai.OllamaClient, dingClient *dingtalk.Client) *Engine {
	return &Engine{
		cfg:        cfg,
		aiClient:   aiClient,
		dingClient: dingClient,
		sessions:   make(map[string]*PendingApproval),
	}
}

func (e *Engine) AnalyzeAndApprove(ctx context.Context, session *models.Session, command string) (*ApprovalRecord, error) {
	record := &ApprovalRecord{
		SessionID: session.ID,
		Command:   command,
		CreatedAt: time.Now(),
		Status:    StatusPending,
	}

	result, err := e.aiClient.AnalyzeCommands(ctx, []string{command})
	if err != nil {
		record.Status = StatusRejected
		record.RiskLevel = 2
		record.Intent = "unknown"
		record.Category = "other"
		record.RiskReason = fmt.Sprintf("AI analysis failed: %v", err)
		return record, fmt.Errorf("AI analysis failed: %w", err)
	}

	record.AIResult = result
	if len(result.Analyses) > 0 {
		a := result.Analyses[0]
		record.RiskLevel = a.RiskLevel
		record.Intent = a.Intent
		record.Category = a.Category
		record.RiskReason = a.RiskReason
	}

	if record.RiskLevel >= e.cfg.Approval.HighRiskThreshold {
		return e.submitApproval(ctx, session, record, command)
	}

	record.Status = StatusApproved
	record.ResolvedAt = time.Now()
	return record, nil
}

func (e *Engine) submitApproval(ctx context.Context, session *models.Session, record *ApprovalRecord, command string) (*ApprovalRecord, error) {
	form := dingtalk.BuildCommandApprovalForm(
		session.ID,
		session.User,
		fmt.Sprintf("%s:%d", session.TargetHost, session.TargetPort),
		[]string{command},
		record.RiskLevel,
	)

	approvers := e.cfg.Approval.Approvers
	if approvers == "" {
		approvers = session.User
	}

	req := dingtalk.ApprovalRequest{
		ProcessCode:         e.cfg.Approval.ProcessCode,
		OriginatorUserID:    session.User,
		Approvers:           approvers,
		FormComponentValues: form,
	}

	processInstanceID, err := e.dingClient.CreateApproval(ctx, req)
	if err != nil {
		if e.cfg.Approval.FailOpen {
			record.Status = StatusApproved
			record.ResolvedAt = time.Now()
			record.RiskReason += " | Approval creation failed, fail-open"
			return record, nil
		}
		record.Status = StatusRejected
		record.ResolvedAt = time.Now()
		record.RiskReason += fmt.Sprintf(" | Approval creation failed: %v", err)
		return record, fmt.Errorf("create approval: %w", err)
	}

	record.ProcessInstanceID = processInstanceID

	pending := &PendingApproval{
		SessionID:         session.ID,
		Command:           command,
		AIResult:          record.AIResult,
		ProcessInstanceID: processInstanceID,
		CreatedAt:         time.Now(),
		ExpiresAt:         time.Now().Add(e.cfg.Approval.Timeout),
		Done:              make(chan struct{}),
	}

	e.mu.Lock()
	e.sessions[session.ID] = pending
	e.mu.Unlock()

	defer func() {
		e.mu.Lock()
		delete(e.sessions, session.ID)
		e.mu.Unlock()
	}()

	timeout := time.NewTimer(e.cfg.Approval.Timeout)
	defer timeout.Stop()

	pollInterval := 10 * time.Second
	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			record.Status = StatusRejected
			record.ResolvedAt = time.Now()
			return record, ctx.Err()

		case <-timeout.C:
			record.Status = StatusTimeout
			record.ResolvedAt = time.Now()
			record.ApproverUserID = "system"
			record.ApproverRemark = "审批超时自动拒绝"
			return record, nil

		case <-ticker.C:
			inst, err := e.dingClient.GetApprovalInstance(ctx, processInstanceID)
			if err != nil {
				continue
			}

			switch inst.Status {
			case dingtalk.ApprovalStatusFinish:
				if inst.Result == dingtalk.ApprovalResultPass {
					record.Status = StatusApproved
					record.ResolvedAt = time.Now()
					if len(inst.OperationRecords) > 0 {
						record.ApproverUserID = inst.OperationRecords[len(inst.OperationRecords)-1].UserID
						record.ApproverRemark = inst.OperationRecords[len(inst.OperationRecords)-1].Remark
					}
					return record, nil
				} else {
					record.Status = StatusRejected
					record.ResolvedAt = time.Now()
					if len(inst.OperationRecords) > 0 {
						record.ApproverUserID = inst.OperationRecords[len(inst.OperationRecords)-1].UserID
						record.ApproverRemark = inst.OperationRecords[len(inst.OperationRecords)-1].Remark
					}
					return record, nil
				}

			case dingtalk.ApprovalStatusTerminate:
				record.Status = StatusRejected
				record.ResolvedAt = time.Now()
				record.ApproverRemark = "审批已终止"
				return record, nil
			}
		}
	}
}

func (e *Engine) GetPending(sessionID string) *PendingApproval {
	e.mu.RLock()
	defer e.mu.RUnlock()
	return e.sessions[sessionID]
}

func (e *Engine) CancelPending(sessionID string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if p, ok := e.sessions[sessionID]; ok {
		close(p.Done)
		delete(e.sessions, sessionID)
	}
}
