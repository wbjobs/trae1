package registry

import (
	"context"
	"encoding/json"
	"fmt"
	"time"

	clientv3 "go.etcd.io/etcd/client/v3"
)

type ServiceIdentity struct {
	SPIFFEID    string    `json:"spiffe_id"`
	Name        string    `json:"name"`
	Selector    string    `json:"selector"`
	Description string    `json:"description,omitempty"`
	RegisteredAt time.Time `json:"registered_at"`
	ExpiresAt   time.Time `json:"expires_at,omitempty"`
	LastSeen    time.Time `json:"last_seen,omitempty"`
}

type Registry struct {
	cli    *clientv3.Client
	prefix string
}

func New(endpoints []string, prefix string) (*Registry, error) {
	cli, err := clientv3.New(clientv3.Config{Endpoints: endpoints, DialTimeout: 5 * time.Second})
	if err != nil {
		return nil, err
	}
	return &Registry{cli: cli, prefix: prefix}, nil
}

func (r *Registry) Close() error { return r.cli.Close() }

func (r *Registry) Register(ctx context.Context, s ServiceIdentity) error {
	if s.RegisteredAt.IsZero() {
		s.RegisteredAt = time.Now().UTC()
	}
	b, err := json.Marshal(s)
	if err != nil {
		return err
	}
	_, err = r.cli.Put(ctx, r.prefix+s.SPIFFEID, string(b))
	return err
}

func (r *Registry) Deregister(ctx context.Context, id string) error {
	_, err := r.cli.Delete(ctx, r.prefix+id)
	return err
}

func (r *Registry) List(ctx context.Context) ([]ServiceIdentity, error) {
	resp, err := r.cli.Get(ctx, r.prefix, clientv3.WithPrefix())
	if err != nil {
		return nil, err
	}
	out := make([]ServiceIdentity, 0, len(resp.Kvs))
	for _, kv := range resp.Kvs {
		var s ServiceIdentity
		if err := json.Unmarshal(kv.Value, &s); err != nil {
			continue
		}
		out = append(out, s)
	}
	return out, nil
}

func (r *Registry) Get(ctx context.Context, id string) (*ServiceIdentity, error) {
	resp, err := r.cli.Get(ctx, r.prefix+id)
	if err != nil {
		return nil, err
	}
	if len(resp.Kvs) == 0 {
		return nil, fmt.Errorf("not found")
	}
	var s ServiceIdentity
	if err := json.Unmarshal(resp.Kvs[0].Value, &s); err != nil {
		return nil, err
	}
	return &s, nil
}
