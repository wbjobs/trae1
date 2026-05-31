package lift

import (
	"context"
	"log"
	"os"
	"time"

	"github.com/tenantnfs/quotad/internal/model"
)

type Store interface {
	ListLifts(ctx context.Context) ([]*model.LiftRequest, error)
	PutLift(ctx context.Context, l *model.LiftRequest) error
	GetLift(ctx context.Context, id string) (*model.LiftRequest, error)
	ListTenants(ctx context.Context) ([]*model.Tenant, error)
	PutTenant(ctx context.Context, t *model.Tenant) error
	GetTenant(ctx context.Context, id string) (*model.Tenant, error)
}

// Manager maintains temporary quota lifts and reverts them after expiry.
type Manager struct {
	store Store
	log   *log.Logger
}

func New(s Store) *Manager {
	return &Manager{
		store: s,
		log:   log.New(os.Stderr, "[lift] ", log.LstdFlags),
	}
}

// Apply processes a lift request (call after approval).
func (m *Manager) Apply(ctx context.Context, lift *model.LiftRequest) error {
	if !lift.Approved {
		return nil
	}
	now := time.Now()
	lift.StartAt = now
	lift.ExpireAt = now.Add(24 * time.Hour)
	return m.store.PutLift(ctx, lift)
}

// Revert restores original tenant quota when lift expires.
func (m *Manager) Revert(ctx context.Context, lift *model.LiftRequest) error {
	t, err := m.store.GetTenant(ctx, lift.TenantID)
	if err != nil {
		return err
	}
	t.CapacityBytes -= lift.ExtraBytes
	t.FileCount -= lift.ExtraFiles
	if err := m.store.PutTenant(ctx, t); err != nil {
		return err
	}
	lift.Restored = true
	return m.store.PutLift(ctx, lift)
}

// Run scans lifts periodically and reverts expired ones.
func (m *Manager) Run(ctx context.Context) {
	t := time.NewTicker(1 * time.Minute)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			lifts, err := m.store.ListLifts(ctx)
			if err != nil {
				m.log.Printf("list lifts: %v", err)
				continue
			}
			now := time.Now()
			for _, l := range lifts {
				if !l.Restored && !l.ExpireAt.IsZero() && now.After(l.ExpireAt) {
					if err := m.Revert(ctx, l); err != nil {
						m.log.Printf("revert lift %s: %v", l.ID, err)
					} else {
						m.log.Printf("reverted lift %s for tenant %s", l.ID, l.TenantID)
					}
				}
			}
		}
	}
}
