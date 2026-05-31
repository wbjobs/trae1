package monitor

import (
	"context"
	"log"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/fsnotify/fsnotify"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

type UsageGetter interface {
	GetUsage(ctx context.Context, id string) (*model.Usage, error)
	PutUsage(ctx context.Context, u *model.Usage) error
}

type Monitor struct {
	cfg   *config.Config
	store UsageGetter

	watcher *fsnotify.Watcher

	mu         sync.RWMutex
	tenantRoot map[string]string

	// per-tenant in-memory counters, guarded by countMu
	countMu  sync.Mutex
	counters map[string]*counter
	// per-path last-known file size, guarded by countMu
	fileSizes map[string]int64

	// full sync trigger channel (buffered 1 to avoid blocking)
	SyncCh chan struct{}

	evProcessed atomic.Int64
	evDropped   atomic.Int64

	log *log.Logger
}

type counter struct {
	usedBytes int64
	usedFiles int64
	dirty     bool
}

func New(cfg *config.Config, st UsageGetter) (*Monitor, error) {
	w, err := fsnotify.NewWatcher()
	if err != nil {
		return nil, err
	}
	w.BufferSize = cfg.InotifyBufSize
	return &Monitor{
		cfg:        cfg,
		store:      st,
		watcher:    w,
		tenantRoot: make(map[string]string),
		counters:   make(map[string]*counter),
		fileSizes:  make(map[string]int64),
		SyncCh:     make(chan struct{}, 1),
		log:        log.New(os.Stderr, "[monitor] ", log.LstdFlags),
	}, nil
}

func (m *Monitor) Close() error {
	return m.watcher.Close()
}

func (m *Monitor) AddTenant(ctx context.Context, t *model.Tenant) error {
	if err := m.watcher.Add(t.ExportPath); err != nil {
		return err
	}
	m.mu.Lock()
	m.tenantRoot[t.ID] = t.ExportPath
	m.mu.Unlock()

	_ = m.walkAndWatch(t.ExportPath)

	m.countMu.Lock()
	if _, ok := m.counters[t.ID]; !ok {
		m.counters[t.ID] = &counter{}
	}
	m.countMu.Unlock()
	return nil
}

func (m *Monitor) walkAndWatch(root string) error {
	return filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return nil
		}
		if info.IsDir() {
			_ = m.watcher.Add(path)
		}
		return nil
	})
}

func (m *Monitor) RemoveTenant(id string) {
	m.mu.RLock()
	root, ok := m.tenantRoot[id]
	m.mu.RUnlock()
	if ok {
		_ = m.watcher.Remove(root)
	}
	m.mu.Lock()
	delete(m.tenantRoot, id)
	m.mu.Unlock()
}

func (m *Monitor) tenantOf(path string) string {
	m.mu.RLock()
	defer m.mu.RUnlock()
	var bestRoot string
	var bestID string
	for id, root := range m.tenantRoot {
		if len(root) > len(bestRoot) && (path == root || (len(path) > len(root) && path[:len(root)+1] == root+string(os.PathSeparator))) {
			bestRoot = root
			bestID = id
		}
	}
	return bestID
}

func (m *Monitor) Run(ctx context.Context) {
	ch := make(chan fsnotify.Event, m.cfg.InotifyChanSize)
	go m.dispatch(ctx, ch)

	for {
		select {
		case <-ctx.Done():
			return
		case ev, ok := <-m.watcher.Events:
			if !ok {
				return
			}
			select {
			case ch <- ev:
			default:
				m.evDropped.Add(1)
				m.log.Printf("event queue overflow (dropped=%d); triggering full sync", m.evDropped.Load())
				select {
				case m.SyncCh <- struct{}{}:
				default:
				}
			}
		case err, ok := <-m.watcher.Errors:
			if !ok {
				return
			}
			m.log.Printf("watcher error: %v", err)
		}
	}
}

func (m *Monitor) dispatch(ctx context.Context, ch <-chan fsnotify.Event) {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			m.flushAll(ctx)
			return
		case ev := <-ch:
			m.handleEvent(ev)
		case <-ticker.C:
			m.flushAll(ctx)
		}
	}
}

func (m *Monitor) handleEvent(ev fsnotify.Event) {
	tenantID := m.tenantOf(ev.Name)
	if tenantID == "" {
		return
	}

	// On Create for a directory, add watch to make inotify recursive
	if ev.Op&fsnotify.Create != 0 {
		if info, err := os.Stat(ev.Name); err == nil && info.IsDir() {
			_ = m.watcher.Add(ev.Name)
		}
	}

	m.applyDelta(tenantID, ev)
	m.evProcessed.Add(1)
}

func (m *Monitor) applyDelta(tenantID string, ev fsnotify.Event) {
	m.countMu.Lock()
	defer m.countMu.Unlock()

	c := m.counters[tenantID]
	if c == nil {
		c = &counter{}
		m.counters[tenantID] = c
	}

	switch {
	case ev.Op&fsnotify.Create != 0:
		if info, err := os.Stat(ev.Name); err == nil {
			if info.IsDir() {
				c.usedFiles++
			} else {
				c.usedFiles++
				c.usedBytes += info.Size()
				m.fileSizes[ev.Name] = info.Size()
			}
			c.dirty = true
		}

	case ev.Op&fsnotify.Write != 0:
		if info, err := os.Stat(ev.Name); err == nil && !info.IsDir() {
			prev := m.fileSizes[ev.Name]
			m.fileSizes[ev.Name] = info.Size()
			c.usedBytes += info.Size() - prev
			c.dirty = true
		}

	case ev.Op&fsnotify.Remove != 0:
		if prev, ok := m.fileSizes[ev.Name]; ok {
			c.usedBytes -= prev
			delete(m.fileSizes, ev.Name)
		}
		c.usedFiles--
		c.dirty = true

	case ev.Op&fsnotify.Rename != 0:
		// Rename within same tenant: old path removed, new path gets Create event.
		// We decrement based on old name; Create will add on new.
		// However Rename is sent for both source and destination; only source has Rename.
		// Best-effort: track old size if available.
		if prev, ok := m.fileSizes[ev.Name]; ok {
			c.usedBytes -= prev
			delete(m.fileSizes, ev.Name)
		}
		c.usedFiles--
		c.dirty = true
	}
}

func (m *Monitor) flushAll(ctx context.Context) {
	m.countMu.Lock()
	for id, c := range m.counters {
		if !c.dirty {
			continue
		}
		u, err := m.store.GetUsage(ctx, id)
		if err == nil {
			u.UsedBytes = c.usedBytes
			u.UsedFiles = c.usedFiles
			u.LastScanTime = time.Now()
			_ = m.store.PutUsage(ctx, u)
		}
		c.dirty = false
	}
	m.countMu.Unlock()
}

func (m *Monitor) ReplaceUsage(ctx context.Context, tenantID string, usedBytes, usedFiles int64) {
	m.countMu.Lock()
	if c, ok := m.counters[tenantID]; ok {
		c.usedBytes = usedBytes
		c.usedFiles = usedFiles
		c.dirty = false
	} else {
		m.counters[tenantID] = &counter{usedBytes: usedBytes, usedFiles: usedFiles}
	}
	m.countMu.Unlock()
}

func (m *Monitor) Stats() (processed, dropped int64) {
	return m.evProcessed.Load(), m.evDropped.Load()
}

func (m *Monitor) ResetLastSize() {
	m.countMu.Lock()
	defer m.countMu.Unlock()
	for k := range m.fileSizes {
		delete(m.fileSizes, k)
	}
}
