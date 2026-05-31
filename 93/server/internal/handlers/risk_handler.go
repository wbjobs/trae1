package handlers

import (
	"encoding/json"
	"net/http"

	"webauthn-auth/internal/middleware"
	"webauthn-auth/internal/risk"
)

type RiskHandler struct {
	engine *risk.RiskEngine
}

func NewRiskHandler(engine *risk.RiskEngine) *RiskHandler {
	return &RiskHandler{engine: engine}
}

type RiskCheckRequest struct {
	Username string `json:"username"`
}

type RiskCheckResponse struct {
	Status        string               `json:"status"`
	Assessment    *risk.RiskAssessment `json:"assessment"`
}

func (h *RiskHandler) CheckRisk(w http.ResponseWriter, r *http.Request) {
	var req RiskCheckRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	if req.Username == "" {
		http.Error(w, "username is required", http.StatusBadRequest)
		return
	}

	assessment := h.engine.AssessRisk(r, req.Username)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(RiskCheckResponse{
		Status:     "success",
		Assessment: assessment,
	})
}

type RiskHistoryResponse struct {
	Status    string              `json:"status"`
	History   *risk.UserHistory   `json:"history,omitempty"`
	HasHistory bool              `json:"hasHistory"`
}

func (h *RiskHandler) GetRiskHistory(w http.ResponseWriter, r *http.Request) {
	username, ok := middleware.GetUsernameFromContext(r.Context())
	if !ok {
		http.Error(w, "unauthorized", http.StatusUnauthorized)
		return
	}

	history, exists := h.engine.GetHistory(username)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(RiskHistoryResponse{
		Status:     "success",
		History:    history,
		HasHistory: exists,
	})
}

type RiskStatsResponse struct {
	Status string                 `json:"status"`
	Stats  map[string]interface{} `json:"stats"`
}

func (h *RiskHandler) GetStats(w http.ResponseWriter, r *http.Request) {
	stats := h.engine.GetStats()

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(RiskStatsResponse{
		Status: "success",
		Stats:  stats,
	})
}

type RiskConfigRequest struct {
	Enabled bool `json:"enabled"`
}

type RiskConfigResponse struct {
	Status  string                 `json:"status"`
	Message string                 `json:"message"`
	Stats   map[string]interface{} `json:"stats"`
}

func (h *RiskHandler) UpdateConfig(w http.ResponseWriter, r *http.Request) {
	var req RiskConfigRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		http.Error(w, "invalid request body", http.StatusBadRequest)
		return
	}

	h.engine.SetEnabled(req.Enabled)

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(RiskConfigResponse{
		Status:  "success",
		Message: "risk engine configuration updated",
		Stats:   h.engine.GetStats(),
	})
}

type RetrainResponse struct {
	Status     string                 `json:"status"`
	Message    string                 `json:"message"`
	Stats      map[string]interface{} `json:"stats,omitempty"`
}

func (h *RiskHandler) RetrainModel(w http.ResponseWriter, r *http.Request) {
	err := h.engine.Retrain()
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(RetrainResponse{
		Status:  "success",
		Message: "model retrained successfully",
		Stats:   h.engine.GetStats(),
	})
}
