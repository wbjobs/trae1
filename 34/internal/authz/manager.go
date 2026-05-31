package authz

import (
	"context"
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"msgbridge/internal/logger"
)

type Manager struct {
	store      *EtcdStore
	verifier   *SVIDVerifier
	serviceID  string
	enabled    bool
}

func NewManager(store *EtcdStore, verifier *SVIDVerifier, serviceID string, enabled bool) *Manager {
	return &Manager{
		store:     store,
		verifier:  verifier,
		serviceID: serviceID,
		enabled:   enabled,
	}
}

func (m *Manager) IsEnabled() bool {
	return m.enabled
}

func (m *Manager) Authorize(ctx context.Context, sourceSVID, targetService, method, path string) (bool, *Policy, string) {
	if !m.enabled {
		return true, nil, "authorization disabled"
	}

	result := m.store.MatchRequest(sourceSVID, targetService, method, path)
	if !result.Matched {
		return false, nil, result.Reason
	}

	if result.Policy.Effect == EffectDeny {
		return false, result.Policy, "explicit deny"
	}

	return true, result.Policy, "allowed"
}

func (m *Manager) Middleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !m.enabled {
			next(w, r)
			return
		}

		authHeader := r.Header.Get("Authorization")
		svidClaims, err := m.verifier.VerifyFromHeader(authHeader)
		if err != nil {
			logger.S.Warnf("SVID verification failed: %v", err)
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		allowed, policy, reason := m.Authorize(
			r.Context(),
			svidClaims.SVID,
			m.serviceID,
			r.Method,
			r.URL.Path,
		)

		if !allowed {
			logger.S.Warnf("Authorization denied: svid=%s method=%s path=%s reason=%s policy=%s",
				svidClaims.SVID, r.Method, r.URL.Path, reason,
				func() string {
					if policy != nil {
						return policy.Name
					}
					return "none"
				}())
			http.Error(w, "forbidden", http.StatusForbidden)
			return
		}

		ctx := context.WithValue(r.Context(), "svid_claims", svidClaims)
		ctx = context.WithValue(ctx, "matched_policy", policy)
		next(w, r.WithContext(ctx))
	}
}

func (m *Manager) AdminMiddleware(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if !m.enabled {
			next(w, r)
			return
		}

		authHeader := r.Header.Get("Authorization")
		svidClaims, err := m.verifier.VerifyFromHeader(authHeader)
		if err != nil {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}

		if svidClaims.Role != "admin" {
			http.Error(w, "admin role required", http.StatusForbidden)
			return
		}

		ctx := context.WithValue(r.Context(), "svid_claims", svidClaims)
		next(w, r.WithContext(ctx))
	}
}

func (m *Manager) ListPolicies(w http.ResponseWriter, r *http.Request) {
	policies := m.store.ListPolicies()
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"total": len(policies),
		"items": policies,
	})
}

func (m *Manager) GetPolicy(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if id == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "id is required"})
		return
	}

	p, ok := m.store.GetPolicy(id)
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "policy not found"})
		return
	}

	writeJSON(w, http.StatusOK, p)
}

type CreatePolicyRequest struct {
	Name        string `json:"name"`
	Description string `json:"description"`
	Source      string `json:"source"`
	SourceType  string `json:"source_type"`
	Target      string `json:"target"`
	TargetType  string `json:"target_type"`
	Method      string `json:"method"`
	PathPattern string `json:"path_pattern"`
	Effect      string `json:"effect"`
	Priority    int    `json:"priority"`
	Enabled     bool   `json:"enabled"`
}

func (m *Manager) CreatePolicy(w http.ResponseWriter, r *http.Request) {
	var req CreatePolicyRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid request body"})
		return
	}

	p := NewPolicy()
	p.Name = req.Name
	p.Description = req.Description
	p.Source = req.Source
	p.SourceType = req.SourceType
	p.Target = req.Target
	p.TargetType = req.TargetType
	p.Method = Action(req.Method)
	p.PathPattern = req.PathPattern
	p.Effect = Effect(req.Effect)
	p.Priority = req.Priority
	p.Enabled = req.Enabled

	if p.Source == "" {
		p.Source = "*"
	}
	if p.Target == "" {
		p.Target = "*"
	}
	if p.Method == "" {
		p.Method = ActionAny
	}
	if p.PathPattern == "" {
		p.PathPattern = "*"
	}
	if p.Effect == "" {
		p.Effect = EffectAllow
	}

	operator := getOperator(r)
	p.CreatedBy = operator

	if err := m.store.SavePolicy(r.Context(), p); err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	auditLog := NewAuditLog(p.ID, "create", operator)
	auditLog.IP = r.RemoteAddr
	if after, err := json.Marshal(p); err == nil {
		auditLog.After = string(after)
	}
	_ = m.store.AddAuditLog(r.Context(), auditLog)

	logger.S.Infof("Policy created: id=%s name=%s by=%s", p.ID, p.Name, operator)
	writeJSON(w, http.StatusCreated, p)
}

func (m *Manager) UpdatePolicy(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if id == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "id is required"})
		return
	}

	existing, ok := m.store.GetPolicy(id)
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "policy not found"})
		return
	}

	var req CreatePolicyRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid request body"})
		return
	}

	existing.Name = req.Name
	existing.Description = req.Description
	existing.Source = req.Source
	existing.SourceType = req.SourceType
	existing.Target = req.Target
	existing.TargetType = req.TargetType
	existing.Method = Action(req.Method)
	existing.PathPattern = req.PathPattern
	existing.Effect = Effect(req.Effect)
	existing.Priority = req.Priority
	existing.Enabled = req.Enabled
	existing.UpdatedAt = time.Now()

	operator := getOperator(r)

	if err := m.store.SavePolicy(r.Context(), existing); err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	auditLog := NewAuditLog(existing.ID, "update", operator)
	auditLog.IP = r.RemoteAddr
	if after, err := json.Marshal(existing); err == nil {
		auditLog.After = string(after)
	}
	_ = m.store.AddAuditLog(r.Context(), auditLog)

	logger.S.Infof("Policy updated: id=%s name=%s by=%s", existing.ID, existing.Name, operator)
	writeJSON(w, http.StatusOK, existing)
}

func (m *Manager) DeletePolicy(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Query().Get("id")
	if id == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "id is required"})
		return
	}

	existing, ok := m.store.GetPolicy(id)
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "policy not found"})
		return
	}

	if err := m.store.DeletePolicy(r.Context(), id); err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	operator := getOperator(r)
	auditLog := NewAuditLog(id, "delete", operator)
	auditLog.IP = r.RemoteAddr
	if before, err := json.Marshal(existing); err == nil {
		auditLog.Before = string(before)
	}
	_ = m.store.AddAuditLog(r.Context(), auditLog)

	logger.S.Infof("Policy deleted: id=%s by=%s", id, operator)
	writeJSON(w, http.StatusOK, map[string]string{"status": "deleted"})
}

func (m *Manager) ListAuditLogs(w http.ResponseWriter, r *http.Request) {
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	offset, _ := strconv.Atoi(r.URL.Query().Get("offset"))
	if limit <= 0 {
		limit = 20
	}
	if limit > 100 {
		limit = 100
	}

	logs := m.store.ListAuditLogs(limit, offset)
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"total": len(logs),
		"items": logs,
	})
}

func (m *Manager) GetStatus(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"enabled":    m.enabled,
		"service_id": m.serviceID,
		"policies":   len(m.store.ListPolicies()),
	})
}

func (m *Manager) RegisterAdminRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/api/v1/admin/authz/status", m.GetStatus)
	mux.HandleFunc("/api/v1/admin/authz/policies", m.ListPolicies)
	mux.HandleFunc("/api/v1/admin/authz/policies/create", m.CreatePolicy)
	mux.HandleFunc("/api/v1/admin/authz/policies/get", m.GetPolicy)
	mux.HandleFunc("/api/v1/admin/authz/policies/update", m.UpdatePolicy)
	mux.HandleFunc("/api/v1/admin/authz/policies/delete", m.DeletePolicy)
	mux.HandleFunc("/api/v1/admin/authz/audit", m.ListAuditLogs)
}

func getOperator(r *http.Request) string {
	if claims, ok := r.Context().Value("svid_claims").(*SVIDClaims); ok && claims != nil {
		return claims.SVID
	}
	return r.RemoteAddr
}

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}