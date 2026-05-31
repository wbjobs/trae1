package api

import (
	"context"
	"encoding/json"
	"log"
	"net"
	"net/http"
	"time"

	"iprep-sync/internal/bgp"
	"iprep-sync/internal/config"
	"iprep-sync/internal/ml"
	"iprep-sync/internal/model"
	"iprep-sync/internal/store"
)

type Server struct {
	cfg      *config.Config
	st       *store.Store
	bgp      *bgp.Manager
	scorer   *ml.Scorer
	fusion   *ml.FusionEngine
	features *ml.FeatureCollector
	http     *http.Server
}

func New(cfg *config.Config, st *store.Store, mgr *bgp.Manager, scorer *ml.Scorer, fusion *ml.FusionEngine, fc *ml.FeatureCollector) *Server {
	return &Server{cfg: cfg, st: st, bgp: mgr, scorer: scorer, fusion: fusion, features: fc}
}

func (s *Server) Start(ctx context.Context) error {
	mux := http.NewServeMux()
	mux.HandleFunc("/lookup", s.handleLookup)
	mux.HandleFunc("/stats", s.handleStats)
	mux.HandleFunc("/healthz", s.handleHealth)
	mux.HandleFunc("/sessions", s.handleSessions)
	mux.HandleFunc("/interrupts", s.handleInterrupts)
	mux.HandleFunc("/ml/stats", s.handleMLStats)
	s.http = &http.Server{
		Addr:    s.cfg.HTTP.Listen,
		Handler: mux,
	}
	ln, err := net.Listen("tcp", s.cfg.HTTP.Listen)
	if err != nil {
		return err
	}
	log.Printf("[http] listening on %s", s.cfg.HTTP.Listen)
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = s.http.Shutdown(shutdownCtx)
	}()
	go func() {
		if err := s.http.Serve(ln); err != nil && err != http.ErrServerClosed {
			log.Printf("[http] serve err: %v", err)
		}
	}()
	return nil
}

func (s *Server) handleLookup(w http.ResponseWriter, r *http.Request) {
	ip := r.URL.Query().Get("ip")
	if ip == "" {
		http.Error(w, `{"error":"missing ip param"}`, http.StatusBadRequest)
		return
	}

	recordFailed := r.URL.Query().Get("failed") == "1"
	if s.features != nil {
		s.features.Record(ip, r.URL.Path, recordFailed)
	}

	ctx, cancel := context.WithTimeout(r.Context(), 3*time.Second)
	defer cancel()
	res, err := s.st.LookupIP(ctx, ip)
	if err != nil {
		http.Error(w, `{"error":"`+err.Error()+`"}`, http.StatusBadRequest)
		return
	}

	if s.features != nil && s.scorer != nil && s.fusion != nil {
		if feat, ok := s.features.ExtractFeatures(ip); ok {
			mlScore, err := s.scorer.Score(feat)
			if err == nil {
				res.ML = mlScore
				res.Fusion = s.fusion.Fuse(res.Level, mlScore)
				res.Level = res.Fusion.FinalLevel
			}
		} else {
			res.Fusion = s.fusion.Fuse(res.Level, nil)
			res.Level = res.Fusion.FinalLevel
		}
	}

	writeJSON(w, res)
}

type statsResp struct {
	Prefixes map[string]int64 `json:"prefixes"`
	Peers    map[string]int64 `json:"peers"`
}

func (s *Server) handleStats(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	prefixes, err := s.st.Stats(ctx)
	if err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
	writeJSON(w, statsResp{
		Prefixes: prefixes,
		Peers:    s.bgp.PeerCounts(),
	})
}

func (s *Server) handleHealth(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), 1*time.Second)
	defer cancel()
	if err := s.st.Client().Ping(ctx).Err(); err != nil {
		http.Error(w, `{"status":"down","error":"redis unavailable"}`, http.StatusServiceUnavailable)
		return
	}
	writeJSON(w, map[string]string{"status": "ok"})
}

func (s *Server) handleSessions(w http.ResponseWriter, r *http.Request) {
	sessions := s.bgp.Sessions()
	type sessionResp struct {
		Name              string `json:"name"`
		Address           string `json:"address"`
		State             string `json:"state"`
		LastState         string `json:"last_state"`
		DownSince         string `json:"down_since,omitempty"`
		LastUp            string `json:"last_up,omitempty"`
		LastRefresh       string `json:"last_refresh,omitempty"`
		InterruptionCount int64  `json:"interruption_count"`
	}
	out := make([]sessionResp, 0, len(sessions))
	for _, v := range sessions {
		sr := sessionResp{
			Name:              v.Name,
			Address:           v.Address,
			State:             v.State.String(),
			LastState:         v.LastState.String(),
			InterruptionCount: v.InterruptionCount,
		}
		if !v.DownSince.IsZero() {
			sr.DownSince = v.DownSince.Format(time.RFC3339)
		}
		if !v.LastUp.IsZero() {
			sr.LastUp = v.LastUp.Format(time.RFC3339)
		}
		if !v.LastRefresh.IsZero() {
			sr.LastRefresh = v.LastRefresh.Format(time.RFC3339)
		}
		out = append(out, sr)
	}
	writeJSON(w, out)
}

func (s *Server) handleInterrupts(w http.ResponseWriter, r *http.Request) {
	logs := s.bgp.InterruptionLogs()
	writeJSON(w, logs)
}

func (s *Server) handleMLStats(w http.ResponseWriter, r *http.Request) {
	resp := map[string]interface{}{
		"enabled":      s.cfg.ML.Enabled,
		"model_path":   s.cfg.ML.ModelPath,
		"model_loaded": s.scorer != nil && s.scorer.Enabled(),
		"bgp_weight":   s.cfg.ML.BGPWeight,
		"ml_weight":    1.0 - s.cfg.ML.BGPWeight,
		"min_samples":  s.cfg.ML.MinSamples,
		"feature_window_seconds": s.cfg.ML.FeatureWindow,
	}
	if s.features != nil {
		resp["tracked_ips"] = s.features.Stats()["tracked_ips"]
	}
	writeJSON(w, resp)
}

func writeJSON(w http.ResponseWriter, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

var _ = model.LevelUnknown
