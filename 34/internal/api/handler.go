package api

import (
	"context"
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"msgbridge/internal/audit"
	"msgbridge/internal/dlq"
	"msgbridge/internal/logger"
	"msgbridge/internal/mq"
	"msgbridge/internal/retry"
)

type Handler struct {
	store  *dlq.DLQStore
	retry  *retry.RetryManager
	audit  *audit.Store
	tdmqFn func() mq.TDMQStatus
}

func NewHandler(store *dlq.DLQStore, rm *retry.RetryManager, auditStore *audit.Store, tdmqFn func() mq.TDMQStatus) *Handler {
	return &Handler{
		store:  store,
		retry:  rm,
		audit:  auditStore,
		tdmqFn: tdmqFn,
	}
}

type ListResponse struct {
	Total int               `json:"total"`
	Items []*dlq.DLQMessage `json:"items"`
}

type TraceLifecycleResponse struct {
	TraceID  string                 `json:"trace_id"`
	Status   string                 `json:"status"`
	Events   []*audit.MessageEvent  `json:"events"`
	CreatedAt time.Time             `json:"created_at"`
	UpdatedAt time.Time             `json:"updated_at"`
}

type TraceListResponse struct {
	Total int              `json:"total"`
	Items []*audit.Lifecycle `json:"items"`
}

type ReplayRequest struct {
	MessageID string `json:"message_id"`
}

type ErrorResponse struct {
	Error string `json:"error"`
}

type StatusResponse struct {
	TDMQ mq.TDMQStatus `json:"tdmq"`
}

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func (h *Handler) ListDLQ(w http.ResponseWriter, r *http.Request) {
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	offset, _ := strconv.Atoi(r.URL.Query().Get("offset"))
	if limit <= 0 {
		limit = 20
	}
	if limit > 100 {
		limit = 100
	}
	if offset < 0 {
		offset = 0
	}
	items := h.store.List(limit, offset)
	writeJSON(w, http.StatusOK, ListResponse{
		Total: len(items),
		Items: items,
	})
}

func (h *Handler) ReplayDLQ(w http.ResponseWriter, r *http.Request) {
	var req ReplayRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "invalid request body"})
		return
	}
	if req.MessageID == "" {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "message_id is required"})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	if err := h.retry.Replay(ctx, req.MessageID); err != nil {
		writeJSON(w, http.StatusInternalServerError, ErrorResponse{Error: err.Error()})
		return
	}

	writeJSON(w, http.StatusOK, map[string]string{"status": "ok", "message_id": req.MessageID})
}

func (h *Handler) GetTraceLifecycle(w http.ResponseWriter, r *http.Request) {
	traceID := r.URL.Query().Get("trace_id")
	if traceID == "" {
		writeJSON(w, http.StatusBadRequest, ErrorResponse{Error: "trace_id is required"})
		return
	}

	lc, ok := h.audit.Get(traceID)
	if !ok {
		writeJSON(w, http.StatusNotFound, ErrorResponse{Error: "trace not found"})
		return
	}

	resp := TraceLifecycleResponse{
		TraceID:   lc.TraceID,
		Status:    lc.Status,
		Events:    lc.Events,
		CreatedAt: lc.CreatedAt,
		UpdatedAt: lc.UpdatedAt,
	}
	writeJSON(w, http.StatusOK, resp)
}

func (h *Handler) ListTraces(w http.ResponseWriter, r *http.Request) {
	limit, _ := strconv.Atoi(r.URL.Query().Get("limit"))
	offset, _ := strconv.Atoi(r.URL.Query().Get("offset"))
	if limit <= 0 {
		limit = 20
	}
	if limit > 100 {
		limit = 100
	}
	if offset < 0 {
		offset = 0
	}
	items := h.audit.List(limit, offset)
	writeJSON(w, http.StatusOK, TraceListResponse{
		Total: len(items),
		Items: items,
	})
}

func (h *Handler) GetStatus(w http.ResponseWriter, _ *http.Request) {
	resp := StatusResponse{}
	if h.tdmqFn != nil {
		resp.TDMQ = h.tdmqFn()
	}
	writeJSON(w, http.StatusOK, resp)
}

func (h *Handler) Health(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (h *Handler) RegisterRoutes(mux *http.ServeMux) {
	mux.HandleFunc("/health", h.Health)
	mux.HandleFunc("/api/v1/status", h.GetStatus)
	mux.HandleFunc("/api/v1/dlq", h.ListDLQ)
	mux.HandleFunc("/api/v1/dlq/replay", h.ReplayDLQ)
	mux.HandleFunc("/api/v1/traces", h.ListTraces)
	mux.HandleFunc("/api/v1/traces/lifecycle", h.GetTraceLifecycle)
}

func Start(addr string, h *Handler) *http.Server {
	mux := http.NewServeMux()
	h.RegisterRoutes(mux)

	mux.Handle("/", http.FileServer(http.Dir("web")))

	srv := &http.Server{
		Addr:         addr,
		Handler:      mux,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 30 * time.Second,
	}

	go func() {
		logger.S.Infof("HTTP server listening on %s", addr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.S.Fatalf("HTTP server error: %v", err)
		}
	}()

	return srv
}