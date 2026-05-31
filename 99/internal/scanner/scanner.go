package scanner

import (
	"context"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

type Store interface {
	ListTenants(ctx context.Context) ([]*model.Tenant, error)
	GetUsage(ctx context.Context, id string) (*model.Usage, error)
	PutUsage(ctx context.Context, u *model.Usage) error
}

type Monitor interface {
	ReplaceUsage(ctx context.Context, tenantID string, usedBytes, usedFiles int64)
	ResetLastSize()
}

type Scanner struct {
	cfg   *config.Config
	store Store
	mon   Monitor
	log   *log.Logger
}

func New(cfg *config.Config, s Store, m Monitor) *Scanner {
	return &Scanner{
		cfg:   cfg,
		store: s,
		mon:   m,
		log:   log.New(os.Stderr, "[scanner] ", log.LstdFlags),
	}
}

// FullSync runs a full quota calibration via du + find for all tenants.
func (sc *Scanner) FullSync(ctx context.Context) error {
	tenants, err := sc.store.ListTenants(ctx)
	if err != nil {
		return err
	}
	sc.log.Printf("starting full sync for %d tenants", len(tenants))
	for _, t := range tenants {
		usedBytes, usedFiles, err := sc.scanTenant(t.ExportPath)
		if err != nil {
			sc.log.Printf("scan tenant %s failed: %v", t.ID, err)
			continue
		}
		sc.mon.ReplaceUsage(ctx, t.ID, usedBytes, usedFiles)
		sc.mon.ResetLastSize()
		if err := sc.store.PutUsage(ctx, &model.Usage{
			TenantID:     t.ID,
			UsedBytes:    usedBytes,
			UsedFiles:    usedFiles,
			LastScanTime: time.Now(),
		}); err != nil {
			sc.log.Printf("put usage tenant %s failed: %v", t.ID, err)
		}
		sc.log.Printf("tenant %s: %d bytes, %d files", t.ID, usedBytes, usedFiles)
	}
	return nil
}

func (sc *Scanner) scanTenant(root string) (int64, int64, error) {
	var usedBytes int64
	var usedFiles int64

	if b, err := exec.Command("du", "-sb", root).Output(); err == nil {
		parts := strings.Fields(string(b))
		if len(parts) >= 2 {
			if v, e := strconv.ParseInt(parts[0], 10, 64); e == nil {
				usedBytes = v
			}
		}
	} else {
		sc.log.Printf("du failed for %s: %v, fallback to walk", root, err)
	}

	if b, err := exec.Command("find", root, "-printf", ".\n").Output(); err == nil {
		usedFiles = int64(strings.Count(string(b), "\n"))
	} else {
		sc.log.Printf("find failed for %s: %v, fallback to walk", root, err)
		// fallback
		_ = filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
			if err != nil {
				return nil
			}
			usedFiles++
			if !info.IsDir() && usedBytes == 0 {
				usedBytes += info.Size()
			}
			return nil
		})
	}
	return usedBytes, usedFiles, nil
}

// Run periodically triggers FullSync (every SyncInterval) and on demand via ch.
func (sc *Scanner) Run(ctx context.Context, trigger <-chan struct{}) {
	t := time.NewTicker(sc.cfg.SyncInterval)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			_ = sc.FullSync(ctx)
		case <-trigger:
			sc.log.Printf("on-demand full sync triggered")
			_ = sc.FullSync(ctx)
		}
	}
}
