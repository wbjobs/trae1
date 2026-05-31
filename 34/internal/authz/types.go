package authz

import (
	"time"

	"github.com/google/uuid"
)

type Action string

const (
	ActionRead   Action = "GET"
	ActionWrite  Action = "POST"
	ActionUpdate Action = "PUT"
	ActionDelete Action = "DELETE"
	ActionAny    Action = "*"
)

type Effect string

const (
	EffectAllow Effect = "allow"
	EffectDeny  Effect = "deny"
)

type Policy struct {
	ID          string    `json:"id"`
	Name        string    `json:"name"`
	Description string    `json:"description"`
	Source      string    `json:"source"`
	SourceType  string    `json:"source_type"`
	Target      string    `json:"target"`
	TargetType  string    `json:"target_type"`
	Method      Action    `json:"method"`
	PathPattern string    `json:"path_pattern"`
	Effect      Effect    `json:"effect"`
	Priority    int       `json:"priority"`
	Enabled     bool      `json:"enabled"`
	CreatedAt   time.Time `json:"created_at"`
	UpdatedAt   time.Time `json:"updated_at"`
	CreatedBy   string    `json:"created_by"`
}

func NewPolicy() *Policy {
	now := time.Now()
	return &Policy{
		ID:        uuid.New().String(),
		Effect:    EffectAllow,
		Enabled:   true,
		CreatedAt: now,
		UpdatedAt: now,
	}
}

type AuditLog struct {
	ID        string    `json:"id"`
	PolicyID  string    `json:"policy_id"`
	Action    string    `json:"action"`
	Operator  string    `json:"operator"`
	Before    string    `json:"before,omitempty"`
	After     string    `json:"after,omitempty"`
	Timestamp time.Time `json:"timestamp"`
	IP        string    `json:"ip,omitempty"`
}

func NewAuditLog(policyID, action, operator string) *AuditLog {
	return &AuditLog{
		ID:        uuid.New().String(),
		PolicyID:  policyID,
		Action:    action,
		Operator:  operator,
		Timestamp: time.Now(),
	}
}

type SVIDClaims struct {
	SVID    string `json:"svid"`
	Service string `json:"service"`
	Group   string `json:"group,omitempty"`
	Role    string `json:"role,omitempty"`
}

type MatchResult struct {
	Matched bool
	Policy  *Policy
	Reason  string
}

func MatchPolicy(p *Policy, sourceSVID, targetService, method, path string) MatchResult {
	if !p.Enabled {
		return MatchResult{Matched: false, Reason: "policy disabled"}
	}

	if p.Source != "*" && p.Source != sourceSVID {
		return MatchResult{Matched: false, Reason: "source mismatch"}
	}

	if p.Target != "*" && p.Target != targetService {
		return MatchResult{Matched: false, Reason: "target mismatch"}
	}

	if p.Method != ActionAny && string(p.Method) != method {
		return MatchResult{Matched: false, Reason: "method mismatch"}
	}

	if !matchPath(p.PathPattern, path) {
		return MatchResult{Matched: false, Reason: "path mismatch"}
	}

	return MatchResult{Matched: true, Policy: p}
}

func matchPath(pattern, path string) bool {
	if pattern == "*" || pattern == "" {
		return true
	}

	if pattern == path {
		return true
	}

	if len(pattern) > 0 && pattern[len(pattern)-1] == '*' {
		prefix := pattern[:len(pattern)-1]
		if len(path) >= len(prefix) && path[:len(prefix)] == prefix {
			return true
		}
	}

	return false
}