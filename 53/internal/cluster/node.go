package cluster

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/rand"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/hashicorp/raft"
	raftboltdb "github.com/hashicorp/raft-boltdb"

	"raftkv/internal/store"
)

type Options struct {
	NodeID   string
	BindAddr string
	DataDir  string
	Store    store.Store
}

type Node struct {
	opts Options
	raft *raft.Raft
	tran raft.Transport
	fsm  *FSM

	confMu       sync.RWMutex
	conf         raft.Configuration
	confVersion  uint64

	cancelRefresh context.CancelFunc
	cancelMonitor context.CancelFunc

	leaderHTTP    atomic.Value

	snapMgr       *SnapshotManager
	logStorePath  string
}

func NewNode(opts Options) (*Node, error) {
	if err := os.MkdirAll(opts.DataDir, 0o755); err != nil {
		return nil, err
	}
	tran, err := raft.NewTCPTransport(opts.BindAddr, nil, 3, 10*time.Second, os.Stderr)
	if err != nil {
		return nil, fmt.Errorf("raft transport: %w", err)
	}

	return &Node{
		opts: opts,
		tran: tran,
	}, nil
}

func (n *Node) Start() error {
	return n.start(false, nil)
}

func (n *Node) Bootstrap(peers map[string]string) error {
	return n.start(true, peers)
}

func (n *Node) start(bootstrap bool, peers map[string]string) error {
	cfg := raft.DefaultConfig()
	cfg.LocalID = raft.ServerID(n.opts.NodeID)
	cfg.SnapshotThreshold = defaultMaxLogEntries
	cfg.SnapshotInterval = 30 * time.Second
	cfg.HeartbeatTimeout = 50 * time.Millisecond
	cfg.LeaderLeaseTimeout = 200 * time.Millisecond
	cfg.ElectionTimeout = randomizedElectionTimeout()
	cfg.CommitTimeout = 15 * time.Millisecond

	logStorePath := filepath.Join(n.opts.DataDir, "logs.db")
	stableStorePath := filepath.Join(n.opts.DataDir, "stable.db")
	snapStorePath := filepath.Join(n.opts.DataDir, "snapshots")
	if err := os.MkdirAll(snapStorePath, 0o755); err != nil {
		return err
	}
	n.logStorePath = logStorePath

	logStore, err := raftboltdb.NewBoltStore(logStorePath)
	if err != nil {
		return fmt.Errorf("log store: %w", err)
	}
	stableStore, err := raftboltdb.NewBoltStore(stableStorePath)
	if err != nil {
		return fmt.Errorf("stable store: %w", err)
	}
	snapStore, err := raft.NewFileSnapshotStore(snapStorePath, 2, os.Stderr)
	if err != nil {
		return fmt.Errorf("snap store: %w", err)
	}

	metaDir := filepath.Join(n.opts.DataDir, "snapshot-meta")
	n.snapMgr = NewSnapshotManager(metaDir, DefaultSnapshotConfig(), snapStore)

	fsm := NewFSM(n.opts.Store)
	n.fsm = fsm

	r, err := raft.NewRaft(cfg, fsm, logStore, stableStore, snapStore, n.tran)
	if err != nil {
		return err
	}
	n.raft = r
	n.snapMgr.SetRaftAPI(&raftAPI{r: r})

	if bootstrap {
		config := raft.Configuration{
			Servers: []raft.Server{
				{
					ID:      raft.ServerID(n.opts.NodeID),
					Address: n.tran.LocalAddr(),
				},
			},
		}
		for id, addr := range peers {
			config.Servers = append(config.Servers, raft.Server{
				ID:      raft.ServerID(id),
				Address: raft.ServerAddress(addr),
			})
		}
		f := r.BootstrapCluster(config)
		if err := f.Error(); err != nil {
			return fmt.Errorf("bootstrap: %w", err)
		}
	}

	ctx, cancel := context.WithCancel(context.Background())
	n.cancelRefresh = cancel
	go n.refreshConfLoop(ctx)

	ctx2, cancel2 := context.WithCancel(context.Background())
	n.cancelMonitor = cancel2
	go n.snapshotMonitorLoop(ctx2)

	return nil
}

func randomizedElectionTimeout() time.Duration {
	const min, max = 150 * time.Millisecond, 300 * time.Millisecond
	return min + time.Duration(rand.Int63n(int64(max-min)))
}

func (n *Node) refreshConfLoop(ctx context.Context) {
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()
	n.refreshConf()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			n.refreshConf()
		}
	}
}

func (n *Node) snapshotMonitorLoop(ctx context.Context) {
	interval := n.snapMgr.Config().CheckInterval
	if interval <= 0 {
		interval = 10 * time.Second
	}
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			n.checkAutoSnapshot()
		}
	}
}

func (n *Node) checkAutoSnapshot() {
	if n.raft == nil {
		return
	}
	if !n.IsLeader() {
		return
	}
	logSize := fileSize(n.logStorePath)
	lastIdx := n.raft.LastIndex()
	applied := n.raft.AppliedIndex()
	var entriesSince uint64
	if applied >= lastIdx {
		entriesSince = 0
	} else {
		entriesSince = lastIdx - applied
	}
	lastAutoIdx, _ := n.snapMgr.LastAuto()
	var entriesSinceSnap uint64
	if applied > lastAutoIdx {
		entriesSinceSnap = applied - lastAutoIdx
	}
	checkEntries := entriesSinceSnap
	if entriesSince > entriesSinceSnap {
		checkEntries = entriesSince
	}
	if n.snapMgr.ShouldAutoSnapshot(logSize, checkEntries) {
		if _, err := n.snapMgr.trigger("auto"); err != nil {
			fmt.Fprintf(os.Stderr, "[%s] auto snapshot failed: %v\n", n.opts.NodeID, err)
		} else {
			fmt.Fprintf(os.Stderr, "[%s] auto snapshot triggered (logSize=%dMB, entriesSinceSnap=%d)\n",
				n.opts.NodeID, logSize/(1024*1024), checkEntries)
			n.snapMgr.prune(3)
		}
	}
}

func fileSize(path string) int64 {
	fi, err := os.Stat(path)
	if err != nil {
		return 0
	}
	return fi.Size()
}

type raftAPI struct {
	r *raft.Raft
}

func (a *raftAPI) LastIndex() uint64    { return a.r.LastIndex() }
func (a *raftAPI) AppliedIndex() uint64 { return a.r.AppliedIndex() }
func (a *raftAPI) State() raft.RaftState { return a.r.State() }
func (a *raftAPI) Snapshot() raft.Future  { return a.r.Snapshot() }
func (a *raftAPI) Stats() map[string]string { return a.r.Stats() }

func (n *Node) refreshConf() {
	if n.raft == nil {
		return
	}
	f := n.raft.GetConfiguration()
	if err := f.Error(); err != nil {
		return
	}
	cfg := f.Configuration()
	n.confMu.Lock()
	if cfg.Index > n.confVersion {
		n.conf = cfg
		n.confVersion = cfg.Index
	}
	n.confMu.Unlock()
}

// Configuration returns the last cached raft configuration (from confState).
func (n *Node) Configuration() raft.Configuration {
	n.confMu.RLock()
	defer n.confMu.RUnlock()
	return n.conf
}

// Peers returns the list of known peer server IDs and addresses.
func (n *Node) Peers() []raft.Server {
	cfg := n.Configuration()
	out := make([]raft.Server, 0, len(cfg.Servers))
	for _, s := range cfg.Servers {
		if s.ID != raft.ServerID(n.opts.NodeID) {
			out = append(out, s)
		}
	}
	return out
}

// SetLeaderHTTP remembers the HTTP address of the known leader (set by client probing).
func (n *Node) SetLeaderHTTP(addr string) { n.leaderHTTP.Store(addr) }
func (n *Node) LeaderHTTP() string {
	v := n.leaderHTTP.Load()
	if v == nil {
		return ""
	}
	return v.(string)
}

func (n *Node) SnapshotManager() *SnapshotManager { return n.snapMgr }

func (n *Node) NodeID() string { return n.opts.NodeID }

func (n *Node) TriggerSnapshot() (SnapshotMeta, error) {
	if n.snapMgr == nil {
		return SnapshotMeta{}, errors.New("snapshot manager not initialized")
	}
	return n.snapMgr.TriggerManual()
}

func (n *Node) ListSnapshots() []SnapshotMeta {
	if n.snapMgr == nil {
		return nil
	}
	return n.snapMgr.List()
}

func (n *Node) SetAutoSnapshot(enabled bool) {
	if n.snapMgr != nil {
		n.snapMgr.SetAutoEnabled(enabled)
	}
}

func (n *Node) FSM() *FSM { return n.fsm }

func (n *Node) TransferState() map[string]*SnapshotTransfer {
	if n.fsm == nil {
		return nil
	}
	return n.fsm.AllTransfers()
}

func (n *Node) Stop() error {
	if n.cancelRefresh != nil {
		n.cancelRefresh()
	}
	if n.cancelMonitor != nil {
		n.cancelMonitor()
	}
	if n.raft != nil {
		f := n.raft.Shutdown()
		_ = f.Error()
	}
	return nil
}

func (n *Node) IsLeader() bool {
	return n.raft != nil && n.raft.State() == raft.Leader
}

func (n *Node) Leader() (string, string) {
	if n.raft == nil {
		return "", ""
	}
	addr, id := n.raft.LeaderWithID()
	return string(id), string(addr)
}

func (n *Node) State() string {
	if n.raft == nil {
		return "stopped"
	}
	return n.raft.State().String()
}

func (n *Node) GetConfiguration() (raft.Configuration, error) {
	if n.raft == nil {
		return raft.Configuration{}, errors.New("raft not started")
	}
	f := n.raft.GetConfiguration()
	if err := f.Error(); err != nil {
		return raft.Configuration{}, err
	}
	return f.Configuration(), nil
}

func (n *Node) Apply(cmd Command, timeout time.Duration) error {
	if n.raft == nil {
		return errors.New("raft not started")
	}
	data, err := json.Marshal(cmd)
	if err != nil {
		return err
	}
	f := n.raft.Apply(data, timeout)
	return f.Error()
}

// AddVoter adds a voting node to the cluster. Must be called on the leader.
func (n *Node) AddVoter(id, addr string) error {
	if n.raft == nil {
		return errors.New("raft not started")
	}
	f := n.raft.AddVoter(raft.ServerID(id), raft.ServerAddress(addr), 0, 0)
	return f.Error()
}

// RemoveServer removes a node from the cluster.
func (n *Node) RemoveServer(id string) error {
	if n.raft == nil {
		return errors.New("raft not started")
	}
	f := n.raft.RemoveServer(raft.ServerID(id), 0, 0)
	return f.Error()
}

// Stats returns runtime raft statistics.
func (n *Node) Stats() map[string]string {
	if n.raft == nil {
		return nil
	}
	return n.raft.Stats()
}

// LastIndex returns the last applied index of the FSM.
func (n *Node) LastIndex() uint64 {
	if n.raft == nil {
		return 0
	}
	return n.raft.AppliedIndex()
}

// JoinViaHTTP calls another node's /cluster/join endpoint to ask the leader to add us.
func (n *Node) JoinViaHTTP(peerHTTP, myID, myRaftAddr string) error {
	body, err := json.Marshal(map[string]string{
		"id":   myID,
		"addr": myRaftAddr,
	})
	if err != nil {
		return err
	}
	url := fmt.Sprintf("http://%s/cluster/join", peerHTTP)
	resp, err := http.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode/100 != 2 {
		b, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("join failed: %s: %s", resp.Status, string(b))
	}
	return nil
}
