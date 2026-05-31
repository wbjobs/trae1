package store

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
	clientv3 "go.etcd.io/etcd/client/v3"

	"github.com/spiffe-gateway/svid-gateway/internal/policy"
)

func newTestStore(t *testing.T, endpoints []string) *EtcdStore {
	t.Helper()
	s, err := NewEtcdStore(endpoints, "/svid-gateway-test/policies/", "/svid-gateway-test/audit/")
	require.NoError(t, err)
	t.Cleanup(func() {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		defer cancel()
		_, _ = s.cli.Delete(ctx, "/svid-gateway-test/", clientv3.WithPrefix())
		_ = s.Close()
	})
	return s
}

func TestPolicyCRUD(t *testing.T) {
	t.Skip("requires etcd running at localhost:2379")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	s := newTestStore(t, []string{"localhost:2379"})

	p := &policy.Policy{
		ID: "test-pol", Name: "Test Policy",
		Source: "spiffe://example.org/ns/svc/a",
		Destination: "spiffe://example.org/ns/svc/b",
		Methods: []string{"GET", "POST"},
		Path: "/api/v1/users", PathType: "exact",
		Effect: policy.EffectAllow, Priority: 100, Enabled: true,
	}

	err := s.Save(ctx, p)
	require.NoError(t, err)

	list, err := s.List(ctx)
	require.NoError(t, err)
	found := false
	for _, pp := range list {
		if pp.ID == "test-pol" {
			found = true
			assert.Equal(t, "Test Policy", pp.Name)
			assert.Equal(t, policy.EffectAllow, pp.Effect)
		}
	}
	assert.True(t, found)

	err = s.Delete(ctx, "test-pol")
	require.NoError(t, err)

	list2, err := s.List(ctx)
	require.NoError(t, err)
	for _, pp := range list2 {
		assert.NotEqual(t, "test-pol", pp.ID)
	}
}

func TestWatch(t *testing.T) {
	t.Skip("requires etcd running at localhost:2379")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	s := newTestStore(t, []string{"localhost:2379"})

	changes := make(chan []*policy.Policy, 1)
	err := s.Watch(ctx, func(ps []*policy.Policy) {
		changes <- ps
	})
	require.NoError(t, err)

	p := &policy.Policy{
		ID: "watch-pol", Name: "Watch Test",
		Source: "*", Destination: "*",
		Methods: []string{"GET"}, Path: "*", PathType: "exact",
		Effect: policy.EffectAllow, Priority: 100, Enabled: true,
	}
	err = s.Save(ctx, p)
	require.NoError(t, err)

	select {
	case ps := <-changes:
		found := false
		for _, pp := range ps {
			if pp.ID == "watch-pol" {
				found = true
			}
		}
		assert.True(t, found)
	case <-time.After(3 * time.Second):
		t.Fatal("timeout waiting for watch change")
	}
}

func TestAppendAuditJSON(t *testing.T) {
	t.Skip("requires etcd running at localhost:2379")

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	s := newTestStore(t, []string{"localhost:2379"})

	err := s.AppendAuditJSON(ctx, []byte(`{"action":"test","time":"2026-01-01T00:00:00Z"}`))
	require.NoError(t, err)

	list, err := s.ListAudit(ctx, 10)
	require.NoError(t, err)
	assert.NotEmpty(t, list)
}
