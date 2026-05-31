package store

import (
	"context"
	"fmt"
	"time"

	clientv3 "go.etcd.io/etcd/client/v3"

	"github.com/spiffe-gateway/svid-gateway/internal/policy"
)

type EtcdStore struct {
	cli          *clientv3.Client
	policyPrefix string
	auditPrefix  string
}

func NewEtcdStore(endpoints []string, policyPrefix, auditPrefix string) (*EtcdStore, error) {
	cli, err := clientv3.New(clientv3.Config{
		Endpoints:   endpoints,
		DialTimeout: 5 * time.Second,
	})
	if err != nil {
		return nil, fmt.Errorf("etcd connect: %w", err)
	}
	return &EtcdStore{cli: cli, policyPrefix: policyPrefix, auditPrefix: auditPrefix}, nil
}

func (s *EtcdStore) Close() error { return s.cli.Close() }

func (s *EtcdStore) policyKey(id string) string { return s.policyPrefix + id }

func (s *EtcdStore) List(ctx context.Context) ([]*policy.Policy, error) {
	resp, err := s.cli.Get(ctx, s.policyPrefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]*policy.Policy, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		p, err := policy.Unmarshal(kv.Value)
		if err != nil {
			continue
		}
		out = append(out, p)
	}
	return out, nil
}

func (s *EtcdStore) Save(ctx context.Context, p *policy.Policy) error {
	b, err := policy.Marshal(p)
	if err != nil {
		return err
	}
	_, err = s.cli.Put(ctx, s.policyKey(p.ID), string(b))
	return err
}

func (s *EtcdStore) Delete(ctx context.Context, id string) error {
	_, err := s.cli.Delete(ctx, s.policyKey(id))
	return err
}

func (s *EtcdStore) Watch(ctx context.Context, onChange func([]*policy.Policy)) error {
	ch := s.cli.Watch(ctx, s.policyPrefix, clientv3.WithPrefix())
	go func() {
		for ev := range ch {
			if ev.Canceled {
				return
			}
			list, err := s.List(ctx)
			if err != nil {
				continue
			}
			onChange(list)
		}
	}()
	return nil
}

func (s *EtcdStore) AppendAuditJSON(ctx context.Context, raw []byte) error {
	key := fmt.Sprintf("%s%d", s.auditPrefix, time.Now().UnixNano())
	_, err := s.cli.Put(ctx, key, string(raw))
	return err
}

func (s *EtcdStore) ListAudit(ctx context.Context, limit int64) ([][]byte, error) {
	opts := []clientv3.OpOption{clientv3.WithPrefix(), clientv3.WithSort(clientv3.SortByKey, clientv3.SortDescend)}
	if limit > 0 {
		opts = append(opts, clientv3.WithLimit(limit))
	}
	resp, err := s.cli.Get(ctx, s.auditPrefix, opts...)
	if err != nil {
		return nil, err
	}
	out := make([][]byte, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		out = append(out, kv.Value)
	}
	return out, nil
}
