package cluster

import (
	"encoding/json"
	"fmt"
	"io"
	"sync"
	"sync/atomic"

	"github.com/hashicorp/raft"

	"raftkv/internal/store"
)

const (
	OpPut    = "put"
	OpDelete = "delete"
)

type Command struct {
	Op    string `json:"op"`
	Key   string `json:"key"`
	Value string `json:"value,omitempty"`
}

const snapshotChunkSize = 1 * 1024 * 1024

type FSM struct {
	store  store.Store

	mu           sync.RWMutex
	appliedCount uint64
	lastSnapshot uint64

	xferMu   sync.Mutex
	xferState map[string]*SnapshotTransfer
}

type SnapshotTransfer struct {
	SnapshotID string
	Offset     int64
	TotalSize  int64
	StartTime  int64
}

func NewFSM(s store.Store) *FSM {
	return &FSM{
		store:     s,
		xferState: make(map[string]*SnapshotTransfer),
	}
}

func (f *FSM) Apply(log *raft.Log) interface{} {
	var c Command
	if err := json.Unmarshal(log.Data, &c); err != nil {
		return fmt.Errorf("failed to unmarshal command: %w", err)
	}
	var err error
	switch c.Op {
	case OpPut:
		err = f.store.Put([]byte(c.Key), []byte(c.Value))
	case OpDelete:
		err = f.store.Delete([]byte(c.Key))
	default:
		return fmt.Errorf("unknown op: %s", c.Op)
	}
	atomic.AddUint64(&f.appliedCount, 1)
	return err
}

func (f *FSM) AppliedCount() uint64 { return atomic.LoadUint64(&f.appliedCount) }
func (f *FSM) LastSnapshot() uint64  { return f.lastSnapshot }

func (f *FSM) Snapshot() (raft.FSMSnapshot, error) {
	return &fsmSnapshot{store: f.store, fsm: f}, nil
}

func (f *FSM) Restore(rc io.ReadCloser) error {
	defer rc.Close()
	type entry struct{ K, V []byte }
	dec := json.NewDecoder(rc)
	for {
		var e entry
		if err := dec.Decode(&e); err == io.EOF {
			break
		} else if err != nil {
			return err
		}
		if e.V == nil {
			if err := f.store.Delete(e.K); err != nil {
				return err
			}
		} else {
			if err := f.store.Put(e.K, e.V); err != nil {
				return err
			}
		}
	}
	return nil
}

func (f *FSM) StartTransfer(snapshotID string, totalSize int64) {
	f.xferMu.Lock()
	defer f.xferMu.Unlock()
	f.xferState[snapshotID] = &SnapshotTransfer{
		SnapshotID: snapshotID,
		TotalSize:  totalSize,
		StartTime:  0,
	}
}

func (f *FSM) UpdateTransferOffset(snapshotID string, offset int64) {
	f.xferMu.Lock()
	defer f.xferMu.Unlock()
	if st, ok := f.xferState[snapshotID]; ok {
		st.Offset = offset
	}
}

func (f *FSM) FinishTransfer(snapshotID string) {
	f.xferMu.Lock()
	defer f.xferMu.Unlock()
	delete(f.xferState, snapshotID)
}

func (f *FSM) TransferState(snapshotID string) (offset, total int64, ok bool) {
	f.xferMu.Lock()
	defer f.xferMu.Unlock()
	if st, ok := f.xferState[snapshotID]; ok {
		return st.Offset, st.TotalSize, true
	}
	return 0, 0, false
}

func (f *FSM) AllTransfers() map[string]*SnapshotTransfer {
	f.xferMu.Lock()
	defer f.xferMu.Unlock()
	out := make(map[string]*SnapshotTransfer, len(f.xferState))
	for k, v := range f.xferState {
		cp := *v
		out[k] = &cp
	}
	return out
}

type fsmSnapshot struct {
	store store.Store
	fsm   *FSM
}

func (s *fsmSnapshot) Persist(sink raft.SnapshotSink) error {
	type entry struct{ K, V []byte }
	enc := json.NewEncoder(sink)
	err := s.store.ForEach(func(k, v []byte) bool {
		if e := enc.Encode(entry{K: k, V: v}); e != nil {
			_ = sink.Cancel()
			return false
		}
		return true
	})
	if err != nil {
		_ = sink.Cancel()
		return err
	}
	return sink.Close()
}

func (s *fsmSnapshot) Release() {}
