package store

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"path"
	"time"

	"go.etcd.io/etcd/client/v3"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

type Store struct {
	cfg    *config.Config
	client *clientv3.Client
}

func New(cfg *config.Config) (*Store, error) {
	cli, err := clientv3.New(clientv3.Config{
		Endpoints:   cfg.EtcdEndpoints,
		DialTimeout: 5 * time.Second,
	})
	if err != nil {
		return nil, fmt.Errorf("etcd connect: %w", err)
	}
	return &Store{cfg: cfg, client: cli}, nil
}

func (s *Store) Close() error {
	return s.client.Close()
}

func (s *Store) tenantKey(id string) string {
	return path.Join(s.cfg.EtcdPrefix, "tenants", id)
}

func (s *Store) usageKey(id string) string {
	return path.Join(s.cfg.EtcdPrefix, "usage", id)
}

func (s *Store) liftKey(id string) string {
	return path.Join(s.cfg.EtcdPrefix, "lifts", id)
}

func (s *Store) migrationKey(id string) string {
	return path.Join(s.cfg.EtcdPrefix, "migrations", id)
}

func (s *Store) PutTenant(ctx context.Context, t *model.Tenant) error {
	if t.ID == "" {
		return errors.New("tenant id is required")
	}
	now := time.Now()
	if t.CreatedAt.IsZero() {
		t.CreatedAt = now
	}
	t.UpdatedAt = now
	data, err := json.Marshal(t)
	if err != nil {
		return err
	}
	_, err = s.client.Put(ctx, s.tenantKey(t.ID), string(data))
	return err
}

func (s *Store) GetTenant(ctx context.Context, id string) (*model.Tenant, error) {
	resp, err := s.client.Get(ctx, s.tenantKey(id))
	if err != nil {
		return nil, err
	}
	if len(resp.Kvs) == 0 {
		return nil, fmt.Errorf("tenant %s not found", id)
	}
	var t model.Tenant
	if err := json.Unmarshal(resp.Kvs[0].Value, &t); err != nil {
		return nil, err
	}
	return &t, nil
}

func (s *Store) DeleteTenant(ctx context.Context, id string) error {
	_, err := s.client.Delete(ctx, s.tenantKey(id))
	return err
}

func (s *Store) ListTenants(ctx context.Context) ([]*model.Tenant, error) {
	prefix := path.Join(s.cfg.EtcdPrefix, "tenants") + "/"
	resp, err := s.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]*model.Tenant, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		var t model.Tenant
		if err := json.Unmarshal(kv.Value, &t); err != nil {
			return nil, err
		}
		out = append(out, &t)
	}
	return out, nil
}

func (s *Store) PutUsage(ctx context.Context, u *model.Usage) error {
	if u.TenantID == "" {
		return errors.New("tenant id is required")
	}
	data, err := json.Marshal(u)
	if err != nil {
		return err
	}
	_, err = s.client.Put(ctx, s.usageKey(u.TenantID), string(data))
	return err
}

func (s *Store) GetUsage(ctx context.Context, id string) (*model.Usage, error) {
	resp, err := s.client.Get(ctx, s.usageKey(id))
	if err != nil {
		return nil, err
	}
	if len(resp.Kvs) == 0 {
		return &model.Usage{TenantID: id}, nil
	}
	var u model.Usage
	if err := json.Unmarshal(resp.Kvs[0].Value, &u); err != nil {
		return nil, err
	}
	return &u, nil
}

func (s *Store) ListUsage(ctx context.Context) ([]*model.Usage, error) {
	prefix := path.Join(s.cfg.EtcdPrefix, "usage") + "/"
	resp, err := s.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]*model.Usage, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		var u model.Usage
		if err := json.Unmarshal(kv.Value, &u); err != nil {
			return nil, err
		}
		out = append(out, &u)
	}
	return out, nil
}

func (s *Store) ListLifts(ctx context.Context) ([]*model.LiftRequest, error) {
	prefix := path.Join(s.cfg.EtcdPrefix, "lifts") + "/"
	resp, err := s.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]*model.LiftRequest, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		var l model.LiftRequest
		if err := json.Unmarshal(kv.Value, &l); err != nil {
			return nil, err
		}
		out = append(out, &l)
	}
	return out, nil
}

func (s *Store) PutLift(ctx context.Context, l *model.LiftRequest) error {
	if l.ID == "" {
		return errors.New("lift id is required")
	}
	data, err := json.Marshal(l)
	if err != nil {
		return err
	}
	_, err = s.client.Put(ctx, s.liftKey(l.ID), string(data))
	return err
}

func (s *Store) GetLift(ctx context.Context, id string) (*model.LiftRequest, error) {
	resp, err := s.client.Get(ctx, s.liftKey(id))
	if err != nil {
		return nil, err
	}
	if len(resp.Kvs) == 0 {
		return nil, fmt.Errorf("lift %s not found", id)
	}
	var l model.LiftRequest
	if err := json.Unmarshal(resp.Kvs[0].Value, &l); err != nil {
		return nil, err
	}
	return &l, nil
}

func (s *Store) Client() *clientv3.Client { return s.client }

func (s *Store) PutMigration(ctx context.Context, m *model.Migration) error {
	if m.ID == "" {
		return errors.New("migration id is required")
	}
	if m.CreatedAt.IsZero() {
		m.CreatedAt = time.Now()
	}
	data, err := json.Marshal(m)
	if err != nil {
		return err
	}
	_, err = s.client.Put(ctx, s.migrationKey(m.ID), string(data))
	return err
}

func (s *Store) GetMigration(ctx context.Context, id string) (*model.Migration, error) {
	resp, err := s.client.Get(ctx, s.migrationKey(id))
	if err != nil {
		return nil, err
	}
	if len(resp.Kvs) == 0 {
		return nil, fmt.Errorf("migration %s not found", id)
	}
	var m model.Migration
	if err := json.Unmarshal(resp.Kvs[0].Value, &m); err != nil {
		return nil, err
	}
	return &m, nil
}

func (s *Store) ListMigrations(ctx context.Context) ([]*model.Migration, error) {
	prefix := path.Join(s.cfg.EtcdPrefix, "migrations") + "/"
	resp, err := s.client.Get(ctx, prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]*model.Migration, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		var m model.Migration
		if err := json.Unmarshal(kv.Value, &m); err != nil {
			return nil, err
		}
		out = append(out, &m)
	}
	return out, nil
}

func (s *Store) DeleteMigration(ctx context.Context, id string) error {
	_, err := s.client.Delete(ctx, s.migrationKey(id))
	return err
}
