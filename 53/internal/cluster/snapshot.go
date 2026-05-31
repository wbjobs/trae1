package cluster

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"sync"
	"time"

	"github.com/hashicorp/raft"
)

const (
	defaultMaxLogSize   = 100 * 1024 * 1024
	defaultMaxLogEntries = 10000
	snapshotMetaFile    = "meta.json"
)

type SnapshotMeta struct {
	ID        string    `json:"id"`
	Index     uint64    `json:"index"`
	Term      uint64    `json:"term"`
	Size      int64     `json:"size"`
	CreatedAt time.Time `json:"created_at"`
	Source    string    `json:"source"`
}

type SnapshotConfig struct {
	MaxLogSize    int64
	MaxLogEntries uint64
	CheckInterval time.Duration
}

func DefaultSnapshotConfig() SnapshotConfig {
	return SnapshotConfig{
		MaxLogSize:    defaultMaxLogSize,
		MaxLogEntries: defaultMaxLogEntries,
		CheckInterval: 10 * time.Second,
	}
}

type SnapshotManager struct {
	mu        sync.RWMutex
	metaDir   string
	metas     map[string]SnapshotMeta
	config    SnapshotConfig

	lastAutoSnapIdx uint64
	lastAutoSnapAt  time.Time

	autoEnabled bool
	snapStore   raft.SnapshotStore

	raftAPI RaftAPI
}

type RaftAPI interface {
	LastIndex() uint64
	AppliedIndex() uint64
	State() raft.RaftState
	Snapshot() raft.Future
	Stats() map[string]string
}

func NewSnapshotManager(metaDir string, cfg SnapshotConfig, snapStore raft.SnapshotStore) *SnapshotManager {
	if cfg.MaxLogSize <= 0 {
		cfg.MaxLogSize = defaultMaxLogSize
	}
	if cfg.MaxLogEntries == 0 {
		cfg.MaxLogEntries = defaultMaxLogEntries
	}
	if cfg.CheckInterval <= 0 {
		cfg.CheckInterval = 10 * time.Second
	}
	sm := &SnapshotManager{
		metaDir:   metaDir,
		metas:     make(map[string]SnapshotMeta),
		config:    cfg,
		autoEnabled: true,
		snapStore:   snapStore,
	}
	_ = os.MkdirAll(metaDir, 0o755)
	sm.loadMetas()
	return sm
}

func (sm *SnapshotManager) SetRaftAPI(api RaftAPI) {
	sm.raftAPI = api
}

func (sm *SnapshotManager) Config() SnapshotConfig {
	return sm.config
}

func (sm *SnapshotManager) SetAutoEnabled(v bool) {
	sm.mu.Lock()
	defer sm.mu.Unlock()
	sm.autoEnabled = v
}

func (sm *SnapshotManager) loadMetas() {
	entries, err := os.ReadDir(sm.metaDir)
	if err != nil {
		return
	}
	for _, e := range entries {
		if e.IsDir() {
			metaPath := filepath.Join(sm.metaDir, e.Name(), snapshotMetaFile)
			data, err := os.ReadFile(metaPath)
			if err != nil {
				continue
			}
			var m SnapshotMeta
			if err := json.Unmarshal(data, &m); err != nil {
				continue
			}
			m.ID = e.Name()
			sm.metas[m.ID] = m
		}
	}
}

func (sm *SnapshotManager) saveMeta(m SnapshotMeta) {
	dir := filepath.Join(sm.metaDir, m.ID)
	_ = os.MkdirAll(dir, 0o755)
	data, _ := json.MarshalIndent(m, "", "  ")
	_ = os.WriteFile(filepath.Join(dir, snapshotMetaFile), data, 0o644)
}

func (sm *SnapshotManager) deleteMeta(id string) {
	delete(sm.metas, id)
	_ = os.RemoveAll(filepath.Join(sm.metaDir, id))
}

func (sm *SnapshotManager) List() []SnapshotMeta {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	out := make([]SnapshotMeta, 0, len(sm.metas))
	for _, m := range sm.metas {
		out = append(out, m)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Index > out[j].Index })
	return out
}

func (sm *SnapshotManager) Record(id string, index, term uint64, size int64, source string) SnapshotMeta {
	m := SnapshotMeta{
		ID:        id,
		Index:     index,
		Term:      term,
		Size:      size,
		CreatedAt: time.Now(),
		Source:    source,
	}
	sm.mu.Lock()
	sm.metas[id] = m
	if source == "auto" {
		sm.lastAutoSnapIdx = index
		sm.lastAutoSnapAt = m.CreatedAt
	}
	sm.mu.Unlock()
	sm.saveMeta(m)
	return m
}

func (sm *SnapshotManager) LastAuto() (uint64, time.Time) {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	return sm.lastAutoSnapIdx, sm.lastAutoSnapAt
}

func (sm *SnapshotManager) TriggerManual() (SnapshotMeta, error) {
	return sm.trigger("manual")
}

func (sm *SnapshotManager) trigger(source string) (SnapshotMeta, error) {
	if sm.raftAPI == nil {
		return SnapshotMeta{}, fmt.Errorf("raft api not set")
	}
	f := sm.raftAPI.Snapshot()
	if err := f.Error(); err != nil {
		return SnapshotMeta{}, fmt.Errorf("raft snapshot: %w", err)
	}
	snapMeta, err := f.Open()
	if err != nil {
		return SnapshotMeta{}, fmt.Errorf("open snapshot: %w", err)
	}
	meta := sm.Record(
		snapMeta.Name,
		snapMeta.Index,
		snapMeta.Term,
		snapMeta.Size,
		source,
	)
	return meta, nil
}

func (sm *SnapshotManager) ShouldAutoSnapshot(logSize int64, logEntries uint64) bool {
	sm.mu.RLock()
	defer sm.mu.RUnlock()
	if !sm.autoEnabled {
		return false
	}
	if logSize >= sm.config.MaxLogSize {
		return true
	}
	if logEntries >= sm.config.MaxLogEntries {
		return true
	}
	return false
}

func (sm *SnapshotManager) prune(maxKeep int) {
	if maxKeep <= 0 {
		return
	}
	sm.mu.Lock()
	defer sm.mu.Unlock()
	all := make([]SnapshotMeta, 0, len(sm.metas))
	for _, m := range sm.metas {
		all = append(all, m)
	}
	sort.Slice(all, func(i, j int) bool { return all[i].Index > all[j].Index })
	for i := maxKeep; i < len(all); i++ {
		sm.deleteMeta(all[i].ID)
	}
}
