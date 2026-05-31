package api

import (
	"context"
	"encoding/json"
	"log"
	"net/http"
	"os"
	"time"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

type Store interface {
	PutTenant(ctx context.Context, t *model.Tenant) error
	GetTenant(ctx context.Context, id string) (*model.Tenant, error)
	DeleteTenant(ctx context.Context, id string) error
	ListTenants(ctx context.Context) ([]*model.Tenant, error)
	GetUsage(ctx context.Context, id string) (*model.Usage, error)
	ListUsage(ctx context.Context) ([]*model.Usage, error)
	PutLift(ctx context.Context, l *model.LiftRequest) error
	GetLift(ctx context.Context, id string) (*model.LiftRequest, error)
}

type Monitor interface {
	AddTenant(ctx context.Context, t *model.Tenant) error
	RemoveTenant(id string)
}

type Scanner interface {
	FullSync(ctx context.Context) error
}

type Notifier interface {
	Notify(ctx context.Context, ev *model.AlertEvent) error
}

type Migrator interface {
	Start(ctx context.Context, mig *model.Migration) error
	Pause(ctx context.Context, id string) error
	Resume(ctx context.Context, id string) error
	Cancel(ctx context.Context, id string) error
	Get(ctx context.Context, id string) (*model.Migration, error)
	List(ctx context.Context) ([]*model.Migration, error)
}

type Server struct {
	cfg     *config.Config
	store   Store
	mon     Monitor
	scanner Scanner
	notif   Notifier
	migr    Migrator
	log     *log.Logger
}

func New(cfg *config.Config, s Store, m Monitor, sc Scanner, n Notifier, mig Migrator) *Server {
	return &Server{
		cfg:     cfg,
		store:   s,
		mon:     m,
		scanner: sc,
		notif:   n,
		migr:    mig,
		log:     log.New(os.Stderr, "[api] ", log.LstdFlags),
	}
}

func (s *Server) Handler() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/tenants", s.handleTenants)
	mux.HandleFunc("/api/v1/tenants/", s.handleTenant)
	mux.HandleFunc("/api/v1/usage", s.handleUsage)
	mux.HandleFunc("/api/v1/usage/", s.handleUsageOne)
	mux.HandleFunc("/api/v1/lifts", s.handleLifts)
	mux.HandleFunc("/api/v1/lifts/", s.handleLift)
	mux.HandleFunc("/api/v1/sync", s.handleSync)
	mux.HandleFunc("/api/v1/migrations", s.handleMigrations)
	mux.HandleFunc("/api/v1/migrations/", s.handleMigration)
	return mux
}

func writeJSON(w http.ResponseWriter, status int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, status int, err error) {
	writeJSON(w, status, map[string]string{"error": err.Error()})
}

func (s *Server) handleTenants(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		ts, err := s.store.ListTenants(r.Context())
		if err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		writeJSON(w, http.StatusOK, ts)
	case http.MethodPost:
		var t model.Tenant
		if err := json.NewDecoder(r.Body).Decode(&t); err != nil {
			writeErr(w, http.StatusBadRequest, err)
			return
		}
		if t.ID == "" || t.Name == "" || t.ExportPath == "" || t.CapacityBytes <= 0 {
			writeErr(w, http.StatusBadRequest, errBadReq)
			return
		}
		if err := s.store.PutTenant(r.Context(), &t); err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		_ = s.mon.AddTenant(r.Context(), &t)
		writeJSON(w, http.StatusCreated, t)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleTenant(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Path[len("/api/v1/tenants/"):]
	if id == "" {
		writeErr(w, http.StatusBadRequest, errBadReq)
		return
	}
	switch r.Method {
	case http.MethodGet:
		t, err := s.store.GetTenant(r.Context(), id)
		if err != nil {
			writeErr(w, http.StatusNotFound, err)
			return
		}
		writeJSON(w, http.StatusOK, t)
	case http.MethodPut:
		t, err := s.store.GetTenant(r.Context(), id)
		if err != nil {
			writeErr(w, http.StatusNotFound, err)
			return
		}
		var patch struct {
			CapacityBytes *int64 `json:"capacity_bytes"`
			FileCount     *int64 `json:"file_count"`
			Name          string `json:"name"`
		}
		if err := json.NewDecoder(r.Body).Decode(&patch); err != nil {
			writeErr(w, http.StatusBadRequest, err)
			return
		}
		if patch.CapacityBytes != nil {
			t.CapacityBytes = *patch.CapacityBytes
		}
		if patch.FileCount != nil {
			t.FileCount = *patch.FileCount
		}
		if patch.Name != "" {
			t.Name = patch.Name
		}
		if err := s.store.PutTenant(r.Context(), t); err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		writeJSON(w, http.StatusOK, t)
	case http.MethodDelete:
		if err := s.store.DeleteTenant(r.Context(), id); err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		s.mon.RemoveTenant(id)
		w.WriteHeader(http.StatusNoContent)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleUsage(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	us, err := s.store.ListUsage(r.Context())
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, us)
}

func (s *Server) handleUsageOne(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	id := r.URL.Path[len("/api/v1/usage/"):]
	u, err := s.store.GetUsage(r.Context(), id)
	if err != nil {
		writeErr(w, http.StatusNotFound, err)
		return
	}
	writeJSON(w, http.StatusOK, u)
}

func (s *Server) handleLifts(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodPost:
		var l model.LiftRequest
		if err := json.NewDecoder(r.Body).Decode(&l); err != nil {
			writeErr(w, http.StatusBadRequest, err)
			return
		}
		if l.ID == "" {
			l.ID = time.Now().Format("20060102150405") + "-lift"
		}
		l.CreatedAt = time.Now()
		if err := s.store.PutLift(r.Context(), &l); err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		writeJSON(w, http.StatusCreated, l)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleLift(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Path[len("/api/v1/lifts/"):]
	switch r.Method {
	case http.MethodGet:
		l, err := s.store.GetLift(r.Context(), id)
		if err != nil {
			writeErr(w, http.StatusNotFound, err)
			return
		}
		writeJSON(w, http.StatusOK, l)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleSync(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	if err := s.scanner.FullSync(r.Context()); err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "synced"})
}

var errBadReq = &httpErr{msg: "bad request", code: http.StatusBadRequest}

type httpErr struct {
	msg  string
	code int
}

func (e *httpErr) Error() string { return e.msg }

func (s *Server) handleMigrations(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		ms, err := s.migr.List(r.Context())
		if err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		writeJSON(w, http.StatusOK, ms)
	case http.MethodPost:
		var m model.Migration
		if err := json.NewDecoder(r.Body).Decode(&m); err != nil {
			writeErr(w, http.StatusBadRequest, err)
			return
		}
		if m.TenantID == "" || m.TargetPath == "" {
			writeErr(w, http.StatusBadRequest, errBadReq)
			return
		}
		if err := s.migr.Start(r.Context(), &m); err != nil {
			writeErr(w, http.StatusInternalServerError, err)
			return
		}
		writeJSON(w, http.StatusCreated, m)
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (s *Server) handleMigration(w http.ResponseWriter, r *http.Request) {
	id := r.URL.Path[len("/api/v1/migrations/"):]
	if id == "" {
		writeErr(w, http.StatusBadRequest, errBadReq)
		return
	}
	switch r.Method {
	case http.MethodGet:
		m, err := s.migr.Get(r.Context(), id)
		if err != nil {
			writeErr(w, http.StatusNotFound, err)
			return
		}
		writeJSON(w, http.StatusOK, m)
	case http.MethodPost:
		// POST /api/v1/migrations/{id}/pause | /resume | /cancel
		action := r.URL.Query().Get("action")
		switch action {
		case "pause":
			if err := s.migr.Pause(r.Context(), id); err != nil {
				writeErr(w, http.StatusBadRequest, err)
				return
			}
			writeJSON(w, http.StatusOK, map[string]string{"status": "paused"})
		case "resume":
			if err := s.migr.Resume(r.Context(), id); err != nil {
				writeErr(w, http.StatusBadRequest, err)
				return
			}
			writeJSON(w, http.StatusOK, map[string]string{"status": "resumed"})
		case "cancel":
			if err := s.migr.Cancel(r.Context(), id); err != nil {
				writeErr(w, http.StatusBadRequest, err)
				return
			}
			writeJSON(w, http.StatusOK, map[string]string{"status": "cancelled"})
		default:
			writeErr(w, http.StatusBadRequest, errBadReq)
		}
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}
