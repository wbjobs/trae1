package httpapi

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"

	"raftkv/internal/cluster"
	"raftkv/internal/store"
)

type Server struct {
	http    *http.Server
	node    *cluster.Node
	store   store.Store

	mu          sync.RWMutex
	peerHTTPs   map[string]string
	peerHTTPTTL time.Time
}

func New(addr string, node *cluster.Node, s store.Store) *Server {
	srv := &Server{node: node, store: s}
	mux := http.NewServeMux()
	mux.HandleFunc("/kv/", srv.handleKV)
	mux.HandleFunc("/cluster/join", srv.handleJoin)
	mux.HandleFunc("/cluster/leave", srv.handleLeave)
	mux.HandleFunc("/cluster/status", srv.handleStatus)
	mux.HandleFunc("/cluster/peers", srv.handlePeers)
	mux.HandleFunc("/cluster/leader", srv.handleLeader)
	mux.HandleFunc("/cluster/snapshot", srv.handleSnapshot)
	mux.HandleFunc("/cluster/snapshots", srv.handleSnapshots)
	mux.HandleFunc("/cluster/snapshot/auto", srv.handleSnapshotAuto)
	mux.HandleFunc("/cluster/snapshot/transfers", srv.handleSnapshotTransfers)
	mux.HandleFunc("/cluster/admin", srv.handleAdmin)
	srv.http = &http.Server{Addr: addr, Handler: mux}
	return srv
}

func (s *Server) ListenAndServe() error { return s.http.ListenAndServe() }
func (s *Server) Close() error {
	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	return s.http.Shutdown(ctx)
}

type kvRequest struct {
	Key   string `json:"key"`
	Value string `json:"value,omitempty"`
}

type apiError struct {
	Error      string `json:"error"`
	LeaderHTTP string `json:"leader_http,omitempty"`
	LeaderID   string `json:"leader_id,omitempty"`
	LeaderAddr string `json:"leader_addr,omitempty"`
}

func writeJSON(w http.ResponseWriter, code int, v interface{}) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, apiError{Error: msg})
}

func (s *Server) writeNotLeaderErr(w http.ResponseWriter) {
	id, addr := s.node.Leader()
	writeJSON(w, http.StatusTemporaryRedirect, apiError{
		Error:      "not leader",
		LeaderID:   id,
		LeaderAddr: addr,
		LeaderHTTP: s.leaderHTTPByRaftAddr(addr),
	})
}

func (s *Server) leaderHTTPByRaftAddr(addr string) string {
	if addr == "" {
		return ""
	}
	s.mu.RLock()
	defer s.mu.RUnlock()
	if s.peerHTTPs != nil {
		if h, ok := s.peerHTTPs[addr]; ok {
			return h
		}
	}
	return ""
}

func (s *Server) handleKV(w http.ResponseWriter, r *http.Request) {
	key := strings.TrimPrefix(r.URL.Path, "/kv/")
	key = strings.TrimLeft(key, "/")
	if key == "" {
		writeErr(w, http.StatusBadRequest, "key is required")
		return
	}

	switch r.Method {
	case http.MethodGet:
		s.get(w, r, key)
	case http.MethodPut, http.MethodPost:
		s.put(w, r, key)
	case http.MethodDelete:
		s.del(w, r, key)
	default:
		w.Header().Set("Allow", "GET, PUT, POST, DELETE")
		writeErr(w, http.StatusMethodNotAllowed, "method not allowed")
	}
}

func (s *Server) get(w http.ResponseWriter, r *http.Request, key string) {
	mode := r.URL.Query().Get("mode")
	if mode == "" {
		mode = "strong"
	}
	if mode == "strong" {
		if !s.node.IsLeader() {
			s.writeNotLeaderErr(w)
			return
		}
		if err := s.node.Raft().Barrier(5 * time.Second).Error(); err != nil {
			writeErr(w, http.StatusInternalServerError, "barrier failed: "+err.Error())
			return
		}
	}
	v, err := s.store.Get([]byte(key))
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	if v == nil {
		writeErr(w, http.StatusNotFound, "not found")
		return
	}
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"key":   key,
		"value": string(v),
		"mode":  mode,
	})
}

func (s *Server) put(w http.ResponseWriter, r *http.Request, key string) {
	if !s.node.IsLeader() {
		s.writeNotLeaderErr(w)
		return
	}
	body, err := io.ReadAll(r.Body)
	if err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	defer r.Body.Close()
	value := string(body)
	if ct := r.Header.Get("Content-Type"); strings.HasPrefix(ct, "application/json") {
		var req kvRequest
		if err := json.Unmarshal(body, &req); err == nil && (req.Value != "" || len(body) == 0) {
			value = req.Value
		}
	}
	cmd := cluster.Command{Op: cluster.OpPut, Key: key, Value: value}
	if err := s.node.Apply(cmd, 5*time.Second); err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"key": key})
}

func (s *Server) del(w http.ResponseWriter, r *http.Request, key string) {
	if !s.node.IsLeader() {
		s.writeNotLeaderErr(w)
		return
	}
	cmd := cluster.Command{Op: cluster.OpDelete, Key: key}
	if err := s.node.Apply(cmd, 5*time.Second); err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"key": key})
}

type joinRequest struct {
	ID   string `json:"id"`
	Addr string `json:"addr"`
}

func (s *Server) handleJoin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST required")
		return
	}
	if !s.node.IsLeader() {
		s.writeNotLeaderErr(w)
		return
	}
	var req joinRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if req.ID == "" || req.Addr == "" {
		writeErr(w, http.StatusBadRequest, "id and addr are required")
		return
	}
	if err := s.node.AddVoter(req.ID, req.Addr); err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

type leaveRequest struct {
	ID string `json:"id"`
}

func (s *Server) handleLeave(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST required")
		return
	}
	if !s.node.IsLeader() {
		s.writeNotLeaderErr(w)
		return
	}
	var req leaveRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if req.ID == "" {
		writeErr(w, http.StatusBadRequest, "id is required")
		return
	}
	if err := s.node.RemoveServer(req.ID); err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *Server) handleStatus(w http.ResponseWriter, r *http.Request) {
	id, addr := s.node.Leader()
	writeJSON(w, http.StatusOK, map[string]interface{}{
		"state":        s.node.State(),
		"is_leader":    s.node.IsLeader(),
		"leader_id":    id,
		"leader_addr":  addr,
		"applied_idx":  s.node.LastIndex(),
		"raft_stats":   s.node.Stats(),
	})
}

func (s *Server) handlePeers(w http.ResponseWriter, r *http.Request) {
	cfg, err := s.node.GetConfiguration()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	type peer struct {
		ID      string `json:"id"`
		Address string `json:"address"`
		Voter   bool   `json:"voter"`
	}
	peers := make([]peer, 0, len(cfg.Servers))
	for _, srv := range cfg.Servers {
		peers = append(peers, peer{
			ID:      string(srv.ID),
			Address: string(srv.Address),
			Voter:   srv.Suffrage == 0,
		})
	}
	writeJSON(w, http.StatusOK, map[string]interface{}{"peers": peers})
}

func (s *Server) handleLeader(w http.ResponseWriter, r *http.Request) {
	id, addr := s.node.Leader()
	writeJSON(w, http.StatusOK, map[string]string{
		"id":   id,
		"addr": addr,
	})
}

func (s *Server) handleSnapshot(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST required")
		return
	}
	meta, err := s.node.TriggerSnapshot()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, meta)
}

func (s *Server) handleSnapshots(w http.ResponseWriter, r *http.Request) {
	snapshots := s.node.ListSnapshots()
	if snapshots == nil {
		snapshots = []cluster.SnapshotMeta{}
	}
	writeJSON(w, http.StatusOK, map[string]interface{}{"snapshots": snapshots})
}

func (s *Server) handleSnapshotAuto(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet {
		cfg := s.node.SnapshotManager().Config()
		writeJSON(w, http.StatusOK, map[string]interface{}{
			"max_log_size_mb":    cfg.MaxLogSize / (1024 * 1024),
			"max_log_entries":    cfg.MaxLogEntries,
			"check_interval_sec": cfg.CheckInterval.Seconds(),
		})
		return
	}
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "GET or POST required")
		return
	}
	var req struct {
		Enabled *bool  `json:"enabled"`
		MaxSize *int64 `json:"max_log_size_mb"`
		MaxEntries *uint64 `json:"max_log_entries"`
	}
	body, _ := io.ReadAll(r.Body)
	defer r.Body.Close()
	if len(body) > 0 {
		if err := json.Unmarshal(body, &req); err != nil {
			writeErr(w, http.StatusBadRequest, err.Error())
			return
		}
	}
	if req.Enabled != nil {
		s.node.SetAutoSnapshot(*req.Enabled)
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (s *Server) handleSnapshotTransfers(w http.ResponseWriter, r *http.Request) {
	transfers := s.node.TransferState()
	if transfers == nil {
		transfers = make(map[string]*cluster.SnapshotTransfer)
	}
	writeJSON(w, http.StatusOK, map[string]interface{}{"transfers": transfers})
}

func (s *Server) handleAdmin(w http.ResponseWriter, r *http.Request) {
	snapshots := s.node.ListSnapshots()
	id, addr := s.node.Leader()

	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	io.WriteString(w, `<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>RaftKV Admin</title>
<style>
body{font-family:Segoe UI,Arial,sans-serif;margin:24px;background:#f5f5f7}
h1{margin:0 0 16px;color:#333}
h2{color:#555;margin:24px 0 8px;font-size:18px}
.card{background:#fff;border-radius:8px;padding:16px 20px;box-shadow:0 1px 3px rgba(0,0,0,.08);margin-bottom:16px}
table{width:100%;border-collapse:collapse;margin-top:8px}
th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #eee;font-size:14px}
th{background:#fafafa;font-weight:600;color:#666}
tr:hover{background:#f9f9fb}
.badge{display:inline-block;padding:2px 8px;border-radius:4px;font-size:12px;font-weight:600}
.badge-leader{background:#e3f2fd;color:#1565c0}
.badge-auto{background:#e8f5e9;color:#2e7d32}
.btn{display:inline-block;padding:6px 14px;background:#1976d2;color:#fff;border:none;border-radius:4px;cursor:pointer;font-size:13px;text-decoration:none}
.btn:hover{background:#1565c0}
.btn-danger{background:#d32f2f}.btn-danger:hover{background:#b71c1c}
.meta{color:#888;font-size:13px}
</style></head><body>
<h1>RaftKV Cluster Admin</h1>
<div class="card">
<h2>Cluster Status</h2>
<table>
<tr><th>Node</th><th>State</th><th>Leader</th><th>Applied Index</th><th>Last Index</th></tr>
`)
	state := s.node.State()
	stateColor := "#333"
	if state == "Leader" {
		stateColor = "#1565c0"
	}
	fmt.Fprintf(w, `<tr><td>%s</td><td style="color:%s;font-weight:600">%s</td><td>%s (%s)</td><td>%d</td><td>%d</td></tr>`,
		s.node.NodeID(), stateColor, state, id, addr, s.node.LastIndex(), s.node.LastIndex())
	io.WriteString(w, `</table></div>
<div class="card">
<h2>Actions</h2>
<p><a class="btn" href="/cluster/snapshots">View Snapshots (JSON)</a>
<button class="btn" onclick="triggerSnap()">Trigger Snapshot Now</button>
<a class="btn btn-danger" href="/cluster/status">Status JSON</a></p>
</div>
<div class="card">
<h2>Snapshots</h2>
`)
	if len(snapshots) == 0 {
		io.WriteString(w, `<p class="meta">No snapshots yet.</p>`)
	} else {
		io.WriteString(w, `<table><tr><th>ID</th><th>Index</th><th>Term</th><th>Size</th><th>Source</th><th>Created</th></tr>`)
		for _, s := range snapshots {
			srcLabel := s.Source
			if s.Source == "auto" {
				srcLabel = `<span class="badge badge-auto">AUTO</span>`
			} else {
				srcLabel = `<span class="badge" style="background:#fff3e0;color:#e65100">MANUAL</span>`
			}
			fmt.Fprintf(w, `<tr><td>%s</td><td>%d</td><td>%d</td><td>%.2f MB</td><td>%s</td><td>%s</td></tr>`,
				s.ID, s.Index, s.Term, float64(s.Size)/(1024*1024), srcLabel,
				s.CreatedAt.Format("2006-01-02 15:04:05"))
		}
		io.WriteString(w, `</table>`)
	}
	io.WriteString(w, `</div>
<script>
function triggerSnap(){
  fetch('/cluster/snapshot',{method:'POST'})
    .then(r=>r.json()).then(d=>{alert('Snapshot created:\n'+JSON.stringify(d,null,2))})
    .catch(e=>alert('Error: '+e));
}
</script>
</body></html>`)
}
