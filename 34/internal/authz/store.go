package authz

import (
	"context"
	"encoding/json"
	"fmt"
	"path"
	"sort"
	"sync"
	"time"

	clientv3 "go.etcd.io/etcd/client/v3"
	"msgbridge/internal/logger"
)

const (
	policyPrefix = "/msgbridge/authz/policies/"
	auditPrefix  = "/msgbridge/authz/audit/"
)

type EtcdStore struct {
	client     *clientv3.Client
	policies   map[string]*Policy
	auditLogs  []*AuditLog
	policyMu   sync.RWMutex
	auditMu    sync.RWMutex
	maxAudit   int
	watchCh    chan struct{}
}

func NewEtcdStore(endpoints []string, maxAudit int) (*EtcdStore, error) {
	if maxAudit <= 0 {
		maxAudit = 10000
	}

	cfg := clientv3.Config{
		Endpoints:   endpoints,
		DialTimeout: 5 * time.Second,
	}

	cli, err := clientv3.New(cfg)
	if err != nil {
		return nil, fmt.Errorf("connect etcd: %w", err)
	}

	store := &EtcdStore{
		client:    cli,
		policies:  make(map[string]*Policy),
		auditLogs: make([]*AuditLog, 0),
		maxAudit:  maxAudit,
		watchCh:   make(chan struct{}),
	}

	if err := store.loadAll(context.Background()); err != nil {
		logger.S.Warnf("Load policies from etcd failed: %v, using empty state", err)
	}

	go store.watch(context.Background())

	return store, nil
}

func (s *EtcdStore) loadAll(ctx context.Context) error {
	resp, err := s.client.Get(ctx, policyPrefix, clientv3.WithPrefix())
	if err != nil {
		return err
	}

	s.policyMu.Lock()
	defer s.policyMu.Unlock()

	for _, kv := range resp.Kvs {
		var p Policy
		if err := json.Unmarshal(kv.Value, &p); err != nil {
			logger.S.Errorf("Unmarshal policy %s: %v", string(kv.Key), err)
			continue
		}
		s.policies[p.ID] = &p
	}

	logger.S.Infof("Loaded %d policies from etcd", len(s.policies))
	return nil
}

func (s *EtcdStore) watch(ctx context.Context) {
	rch := s.client.Watch(ctx, policyPrefix, clientv3.WithPrefix())
	for wresp := range rch {
		for _, ev := range wresp.Events {
			key := string(ev.Kv.Key)
			id := path.Base(key)

			s.policyMu.Lock()
			switch ev.Type {
			case clientv3.EventTypePut:
				var p Policy
				if err := json.Unmarshal(ev.Kv.Value, &p); err == nil {
					s.policies[id] = &p
					logger.S.Infof("Policy %s updated via watch", id)
				}
			case clientv3.EventTypeDelete:
				delete(s.policies, id)
				logger.S.Infof("Policy %s deleted via watch", id)
			}
			s.policyMu.Unlock()
		}
	}
}

func (s *EtcdStore) SavePolicy(ctx context.Context, p *Policy) error {
	s.policyMu.Lock()
	s.policies[p.ID] = p
	s.policyMu.Unlock()

	data, err := json.Marshal(p)
	if err != nil {
		return fmt.Errorf("marshal policy: %w", err)
	}

	key := policyPrefix + p.ID
	_, err = s.client.Put(ctx, key, string(data))
	if err != nil {
		return fmt.Errorf("put policy to etcd: %w", err)
	}

	return nil
}

func (s *EtcdStore) DeletePolicy(ctx context.Context, id string) error {
	s.policyMu.Lock()
	delete(s.policies, id)
	s.policyMu.Unlock()

	key := policyPrefix + id
	_, err := s.client.Delete(ctx, key)
	if err != nil {
		return fmt.Errorf("delete policy from etcd: %w", err)
	}

	return nil
}

func (s *EtcdStore) GetPolicy(id string) (*Policy, bool) {
	s.policyMu.RLock()
	defer s.policyMu.RUnlock()
	p, ok := s.policies[id]
	return p, ok
}

func (s *EtcdStore) ListPolicies() []*Policy {
	s.policyMu.RLock()
	defer s.policyMu.RUnlock()

	var policies []*Policy
	for _, p := range s.policies {
		policies = append(policies, p)
	}

	sort.Slice(policies, func(i, j int) bool {
		if policies[i].Priority != policies[j].Priority {
			return policies[i].Priority > policies[j].Priority
		}
		return policies[i].CreatedAt.After(policies[j].CreatedAt)
	})

	return policies
}

func (s *EtcdStore) MatchRequest(sourceSVID, targetService, method, path string) MatchResult {
	s.policyMu.RLock()
	defer s.policyMu.RUnlock()

	var policies []*Policy
	for _, p := range s.policies {
		policies = append(policies, p)
	}

	sort.Slice(policies, func(i, j int) bool {
		return policies[i].Priority > policies[j].Priority
	})

	for _, p := range policies {
		result := MatchPolicy(p, sourceSVID, targetService, method, path)
		if result.Matched {
			return result
		}
	}

	return MatchResult{Matched: false, Reason: "no matching policy"}
}

func (s *EtcdStore) AddAuditLog(ctx context.Context, log *AuditLog) error {
	s.auditMu.Lock()
	if len(s.auditLogs) >= s.maxAudit {
		s.auditLogs = s.auditLogs[1:]
	}
	s.auditLogs = append(s.auditLogs, log)
	s.auditMu.Unlock()

	data, err := json.Marshal(log)
	if err != nil {
		return err
	}

	key := auditPrefix + log.ID
	_, err = s.client.Put(ctx, key, string(data))
	if err != nil {
		return fmt.Errorf("put audit log to etcd: %w", err)
	}

	return nil
}

func (s *EtcdStore) ListAuditLogs(limit, offset int) []*AuditLog {
	s.auditMu.RLock()
	defer s.auditMu.RUnlock()

	logs := make([]*AuditLog, len(s.auditLogs))
	copy(logs, s.auditLogs)

	sort.Slice(logs, func(i, j int) bool {
		return logs[i].Timestamp.After(logs[j].Timestamp)
	})

	if offset >= len(logs) {
		return nil
	}
	end := offset + limit
	if end > len(logs) {
		end = len(logs)
	}
	return logs[offset:end]
}

func (s *EtcdStore) Close() {
	if s.client != nil {
		s.client.Close()
	}
}