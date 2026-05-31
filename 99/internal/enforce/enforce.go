package enforce

import (
	"context"
	"errors"
	"syscall"

	"github.com/tenantnfs/quotad/internal/model"
)

// Enforcer is called from the FSAL (File System Abstraction Layer) of
// nfs-ganesha to check quota before writing. It returns ENOSPC (syscall.ENOSPC)
// when the write would exceed quota. This is the real-time enforcement point.
//
// The FSAL module in nfs-ganesha loads this as a shared library or RPC service,
// calling CheckWrite before each CREATE/WRITE operation.
type Enforcer struct {
	store  Store
	lift   LiftStore
	usage  UsageStore
}

type Store interface {
	GetTenant(ctx context.Context, id string) (*model.Tenant, error)
}
type LiftStore interface {
	ListLifts(ctx context.Context) ([]*model.LiftRequest, error)
}
type UsageStore interface {
	GetUsage(ctx context.Context, id string) (*model.Usage, error)
}

func New(s Store, l LiftStore, u UsageStore) *Enforcer {
	return &Enforcer{store: s, lift: l, usage: u}
}

// EffectiveQuota returns the capacity including any active temporary lifts.
func (e *Enforcer) EffectiveQuota(ctx context.Context, tenantID string) (*model.EffectiveQuota, error) {
	t, err := e.store.GetTenant(ctx, tenantID)
	if err != nil {
		return nil, err
	}
	eq := &model.EffectiveQuota{
		CapacityBytes: t.CapacityBytes,
		FileCount:     t.FileCount,
	}
	lifts, err := e.lift.ListLifts(ctx)
	if err != nil {
		return eq, nil
	}
	for _, l := range lifts {
		if l.TenantID == tenantID && l.Approved && !l.Restored && !l.ExpireAt.IsZero() {
			eq.CapacityBytes += l.ExtraBytes
			eq.FileCount += l.ExtraFiles
		}
	}
	return eq, nil
}

// CheckWrite validates that an incoming write of size `bytes` and creating
// `newFiles` new entries would still fit the quota. Returns syscall.ENOSPC
// (wrapped) when over-limit.
func (e *Enforcer) CheckWrite(ctx context.Context, tenantID string, addBytes int64, addFiles int64) error {
	eq, err := e.EffectiveQuota(ctx, tenantID)
	if err != nil {
		return err
	}
	u, err := e.usage.GetUsage(ctx, tenantID)
	if err != nil {
		return err
	}
	if eq.CapacityBytes > 0 && u.UsedBytes+addBytes > eq.CapacityBytes {
		return errors.New("ENOSPC: capacity quota exceeded")
	}
	if eq.FileCount > 0 && u.UsedFiles+addFiles > eq.FileCount {
		return errors.New("ENOSPC: file count quota exceeded")
	}
	return nil
}

// ENOSPC returns the standard syscall errno for FSAL propagation.
func ENOSPC() syscall.Errno { return syscall.ENOSPC }
