package approval

import (
	"context"
	"fmt"
	"time"

	"bastion/internal/config"
	"bastion/internal/models"
	"bastion/internal/ssh"
)

type Interceptor struct {
	cfg    *config.Config
	engine *Engine
}

func NewInterceptor(cfg *config.Config, engine *Engine) *Interceptor {
	return &Interceptor{
		cfg:    cfg,
		engine: engine,
	}
}

func (i *Interceptor) Intercept(command string, session *models.Session) (bool, string) {
	ctx := context.Background()

	record, err := i.engine.AnalyzeAndApprove(ctx, session, command)
	if err != nil {
		session.AddApprovalRecord(models.ApprovalRecordItem{
			Command:    command,
			Status:     "rejected",
			CreatedAt:  time.Now(),
			ResolvedAt: time.Now(),
			RiskReason: fmt.Sprintf("分析失败: %v", err),
		})
		if i.cfg.Approval.FailOpen {
			return true, ""
		}
		return false, fmt.Sprintf("[审批系统故障] 命令审批失败: %v", err)
	}

	item := models.ApprovalRecordItem{
		Command:           command,
		RiskLevel:         record.RiskLevel,
		Intent:            record.Intent,
		Category:          record.Category,
		RiskReason:        record.RiskReason,
		ProcessInstanceID: record.ProcessInstanceID,
		CreatedAt:         record.CreatedAt,
		ResolvedAt:        record.ResolvedAt,
		Status:            record.Status,
		ApproverUserID:    record.ApproverUserID,
		ApproverRemark:    record.ApproverRemark,
	}
	session.AddApprovalRecord(item)

	if record.AIResult != nil && len(record.AIResult.Analyses) > 0 {
		a := record.AIResult.Analyses[0]
		session.AddAIResult(models.AIRiskResult{
			Command:    command,
			RiskLevel:  a.RiskLevel,
			Intent:     a.Intent,
			Category:   a.Category,
			RiskReason: a.RiskReason,
			AnalyzedAt: record.AIResult.AnalyzedAt,
			Model:      record.AIResult.Model,
		})
	}

	switch record.Status {
	case StatusApproved:
		return true, ""
	case StatusRejected:
		return false, fmt.Sprintf("[审批拒绝] 命令已被拒绝: %s", command)
	case StatusTimeout:
		return false, fmt.Sprintf("[审批超时] 审批超时自动拒绝: %s", command)
	default:
		return false, fmt.Sprintf("[审批异常] 未知审批状态: %s", record.Status)
	}
}

func (i *Interceptor) OnSessionComplete(session *models.Session) {
	i.engine.CancelPending(session.ID)
}

var _ ssh.CommandInterceptor = (*Interceptor)(nil)
