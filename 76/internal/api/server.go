package api

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"bastion/internal/config"
	"bastion/internal/models"
	"bastion/internal/storage"
	"bastion/internal/terminal"
)

type Server struct {
	cfg        *config.Config
	store      *models.SessionStore
	minio      *storage.MinIOClient
	httpServer *http.Server
}

type APIResponse struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
}

func NewServer(cfg *config.Config, store *models.SessionStore, minio *storage.MinIOClient) *Server {
	return &Server{
		cfg:   cfg,
		store: store,
		minio: minio,
	}
}

func (s *Server) Start() error {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/sessions", s.handleSessions)
	mux.HandleFunc("/api/reports", s.handleReports)

	s.httpServer = &http.Server{
		Addr:         s.cfg.API.ListenAddr,
		Handler:      mux,
		ReadTimeout:  30 * time.Second,
		WriteTimeout: 0,
	}

	fmt.Printf("[API] HTTP server listening on %s\n", s.cfg.API.ListenAddr)
	return s.httpServer.ListenAndServe()
}

func (s *Server) Shutdown(ctx context.Context) error {
	if s.httpServer != nil {
		return s.httpServer.Shutdown(ctx)
	}
	return nil
}

func (s *Server) handleSessions(w http.ResponseWriter, r *http.Request) {
	path := strings.TrimPrefix(r.URL.Path, "/api/sessions")

	if path == "" || path == "/" {
		s.handleListSessions(w, r)
		return
	}

	path = strings.TrimPrefix(path, "/")
	parts := strings.SplitN(path, "/", 2)
	id := parts[0]

	session, ok := s.store.Get(id)
	if !ok {
		writeError(w, http.StatusNotFound, "session not found: "+id)
		return
	}

	if len(parts) == 1 {
		writeSuccess(w, session)
		return
	}

	subpath := parts[1]
	switch {
	case subpath == "url":
		s.handleSessionReplayURL(w, r, session)
	case subpath == "stream":
		mode := r.URL.Query().Get("mode")
		if mode == "rendered" {
			s.handleSessionStreamRendered(w, r, session)
		} else {
			s.handleSessionStream(w, r, session)
		}
	case subpath == "commands":
		writeSuccess(w, session.Commands)
	case subpath == "frame":
		s.handleSessionFrame(w, r, session)
	case subpath == "frames":
		s.handleSessionFrames(w, r, session)
	case subpath == "search":
		s.handleSessionSearch(w, r, session)
	case subpath == "rendered":
		s.handleSessionRendered(w, r, session)
	case subpath == "report":
		s.handleSessionReport(w, r, session)
	case subpath == "ai-results":
		writeSuccess(w, session.GetAIResults())
	case subpath == "approvals":
		writeSuccess(w, session.GetApprovalRecords())
	default:
		writeError(w, http.StatusNotFound, "unknown endpoint: "+subpath)
	}
}

func (s *Server) handleListSessions(w http.ResponseWriter, r *http.Request) {
	sessions := s.store.List()
	writeSuccess(w, sessions)
}

func (s *Server) handleSessionFrame(w http.ResponseWriter, r *http.Request, session *models.Session) {
	timeStr := r.URL.Query().Get("time")
	mode := r.URL.Query().Get("mode")
	if mode == "" {
		mode = "rendered"
	}

	ts, err := strconv.ParseFloat(timeStr, 64)
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid time parameter")
		return
	}

	fs := wrapFrameStore(session.FrameStore)
	if fs == nil {
		writeError(w, http.StatusBadRequest, "frame store not available")
		return
	}

	frame := fs.GetAt(ts)
	if frame == nil {
		writeError(w, http.StatusNotFound, "no frame at this timestamp")
		return
	}

	if mode == "raw" {
		writeSuccess(w, map[string]interface{}{
			"timestamp": frame.Timestamp,
			"width":     frame.Width,
			"height":    frame.Height,
			"cells":     frame.Cells,
		})
	} else {
		writeSuccess(w, map[string]interface{}{
			"timestamp": frame.Timestamp,
			"width":     frame.Width,
			"height":    frame.Height,
			"text":      frame.RenderText(),
			"lines":     renderFrameLines(frame),
		})
	}
}

func (s *Server) handleSessionFrames(w http.ResponseWriter, r *http.Request, session *models.Session) {
	fromStr := r.URL.Query().Get("from")
	toStr := r.URL.Query().Get("to")
	mode := r.URL.Query().Get("mode")
	if mode == "" {
		mode = "rendered"
	}

	fromTs, _ := strconv.ParseFloat(fromStr, 64)
	toTs, _ := strconv.ParseFloat(toStr, 64)

	fs := wrapFrameStore(session.FrameStore)
	if fs == nil {
		writeError(w, http.StatusBadRequest, "frame store not available")
		return
	}

	frames := fs.GetRange(fromTs, toTs)
	if frames == nil {
		frames = []terminal.Frame{}
	}

	type FrameResult struct {
		Timestamp float64           `json:"timestamp"`
		Width     int               `json:"width"`
		Height    int               `json:"height"`
		Text      string            `json:"text,omitempty"`
		Lines     []string          `json:"lines,omitempty"`
		Cells     []terminal.Cell   `json:"cells,omitempty"`
	}

	results := make([]FrameResult, 0, len(frames))
	for _, f := range frames {
		fr := FrameResult{
			Timestamp: f.Timestamp,
			Width:     f.Width,
			Height:    f.Height,
		}
		if mode == "raw" {
			fr.Cells = f.Cells
		} else {
			fr.Text = f.RenderText()
			fr.Lines = renderFrameLines(&f)
		}
		results = append(results, fr)
	}

	writeSuccess(w, map[string]interface{}{
		"frames": results,
		"count":  len(results),
	})
}

func (s *Server) handleSessionSearch(w http.ResponseWriter, r *http.Request, session *models.Session) {
	query := r.URL.Query().Get("q")
	if query == "" {
		writeError(w, http.StatusBadRequest, "search query is required")
		return
	}

	fromStr := r.URL.Query().Get("from")
	fromTs, _ := strconv.ParseFloat(fromStr, 64)

	maxStr := r.URL.Query().Get("max")
	maxResults, _ := strconv.Atoi(maxStr)
	if maxResults <= 0 {
		maxResults = 50
	}

	fs := wrapFrameStore(session.FrameStore)
	if fs == nil {
		writeError(w, http.StatusBadRequest, "frame store not available")
		return
	}

	results := fs.Search(query, fromTs, maxResults)
	if results == nil {
		results = []terminal.SearchResult{}
	}

	writeSuccess(w, map[string]interface{}{
		"query":   query,
		"results": results,
		"count":   len(results),
	})
}

func (s *Server) handleSessionRendered(w http.ResponseWriter, r *http.Request, session *models.Session) {
	mode := r.URL.Query().Get("mode")
	if mode == "" {
		mode = "rendered"
	}

	fs := wrapFrameStore(session.FrameStore)
	if fs == nil {
		writeError(w, http.StatusBadRequest, "frame store not available")
		return
	}

	frames := fs.GetAll()
	if len(frames) == 0 {
		writeError(w, http.StatusNotFound, "no frames available")
		return
	}

	frame := &frames[len(frames)-1]

	if mode == "raw" {
		writeSuccess(w, map[string]interface{}{
			"timestamp": frame.Timestamp,
			"width":     frame.Width,
			"height":    frame.Height,
			"cells":     frame.Cells,
		})
	} else {
		writeSuccess(w, map[string]interface{}{
			"timestamp": frame.Timestamp,
			"width":     frame.Width,
			"height":    frame.Height,
			"text":      frame.RenderText(),
			"lines":     renderFrameLines(frame),
		})
	}
}

func renderFrameLines(frame *terminal.Frame) []string {
	lines := make([]string, frame.Height)
	for y := 0; y < frame.Height; y++ {
		lines[y] = frame.RenderLine(y)
	}
	return lines
}

type frameStoreGetter struct {
	frames []terminal.Frame
}

func (fsg *frameStoreGetter) GetAll() []terminal.Frame {
	return fsg.frames
}

func (fsg *frameStoreGetter) GetAt(ts float64) *terminal.Frame {
	return terminal.FrameFromTimestamp(fsg.frames, ts)
}

func (fsg *frameStoreGetter) GetRange(from, to float64) []terminal.Frame {
	var result []terminal.Frame
	for _, f := range fsg.frames {
		if f.Timestamp >= from && f.Timestamp <= to {
			result = append(result, f)
		}
	}
	return result
}

func (fsg *frameStoreGetter) Search(query string, fromTs float64, maxResults int) []terminal.SearchResult {
	return terminal.SearchFrames(fsg.frames, query, fromTs, maxResults)
}

func (fsg *frameStoreGetter) Len() int {
	return len(fsg.frames)
}

func wrapFrameStore(fs interface{}) *frameStoreGetter {
	type hasFrames interface {
		GetAll() []terminal.Frame
	}
	if hf, ok := fs.(hasFrames); ok {
		return &frameStoreGetter{frames: hf.GetAll()}
	}
	return nil
}

func writeJSON(w http.ResponseWriter, statusCode int, resp APIResponse) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(statusCode)
	json.NewEncoder(w).Encode(resp)
}

func writeError(w http.ResponseWriter, statusCode int, message string) {
	writeJSON(w, statusCode, APIResponse{
		Code:    statusCode,
		Message: message,
	})
}

func writeSuccess(w http.ResponseWriter, data interface{}) {
	writeJSON(w, http.StatusOK, APIResponse{
		Code:    http.StatusOK,
		Message: "success",
		Data:    data,
	})
}
