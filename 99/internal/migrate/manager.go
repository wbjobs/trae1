package migrate

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/tenantnfs/quotad/internal/model"
)

type Store interface {
	PutTenant(ctx context.Context, t *model.Tenant) error
	GetTenant(ctx context.Context, id string) (*model.Tenant, error)
	PutMigration(ctx context.Context, m *model.Migration) error
	GetMigration(ctx context.Context, id string) (*model.Migration, error)
	ListMigrations(ctx context.Context) ([]*model.Migration, error)
	DeleteMigration(ctx context.Context, id string) error
	GetUsage(ctx context.Context, id string) (*model.Usage, error)
	PutUsage(ctx context.Context, u *model.Usage) error
}

type Monitor interface {
	AddTenant(ctx context.Context, t *model.Tenant) error
	RemoveTenant(id string)
	ReplaceUsage(ctx context.Context, tenantID string, usedBytes, usedFiles int64)
}

type activeMigration struct {
	cancel context.CancelFunc
	pause  chan struct{}
}

type Manager struct {
	store  Store
	mon    Monitor
	log    *log.Logger

	mu     sync.Mutex
	active map[string]*activeMigration
}

func New(s Store, m Monitor) *Manager {
	return &Manager{
		store:  s,
		mon:    m,
		log:    log.New(os.Stderr, "[migrate] ", log.LstdFlags),
		active: make(map[string]*activeMigration),
	}
}

// Start creates a new migration and begins the background copy process.
func (mgr *Manager) Start(ctx context.Context, mig *model.Migration) error {
	if mig.ID == "" {
		mig.ID = fmt.Sprintf("mig-%s", time.Now().Format("20060102150405"))
	}
	if mig.Strategy == "" {
		mig.Strategy = "rsync"
	}
	mig.Status = model.MigrationPending
	mig.CreatedAt = time.Now()

	t, err := mgr.store.GetTenant(ctx, mig.TenantID)
	if err != nil {
		return fmt.Errorf("load tenant: %w", err)
	}
	if mig.SourcePath == "" {
		mig.SourcePath = t.ExportPath
	}
	if mig.TargetPath == "" {
		return fmt.Errorf("target_path is required")
	}

	// Ensure source is a real directory (not a symlink chain we can't resolve)
	srcReal, err := filepath.EvalSymlinks(mig.SourcePath)
	if err != nil {
		return fmt.Errorf("resolve source: %w", err)
	}
	mig.SourcePath = srcReal

	if err := mgr.store.PutMigration(ctx, mig); err != nil {
		return err
	}

	// Launch migration goroutine
	ctx, cancel := context.WithCancel(ctx)
	am := &activeMigration{cancel: cancel, pause: make(chan struct{})}
	close(am.pause) // start in running state (closed = not paused)

	mgr.mu.Lock()
	mgr.active[mig.ID] = am
	mgr.mu.Unlock()

	go mgr.run(ctx, mig, am)
	return nil
}

// Pause pauses a running migration.
func (mgr *Manager) Pause(ctx context.Context, id string) error {
	mgr.mu.Lock()
	am, ok := mgr.active[id]
	mgr.mu.Unlock()
	if !ok {
		return fmt.Errorf("migration %s not running", id)
	}
	m, err := mgr.store.GetMigration(ctx, id)
	if err != nil {
		return err
	}
	if m.Status != model.MigrationRunning {
		return fmt.Errorf("can only pause running migrations (status=%s)", m.Status)
	}

	// Re-create pause channel (un-closed = paused)
	am.pause = make(chan struct{})
	m.Status = model.MigrationPaused
	m.PausedAt = time.Now()
	if err := mgr.store.PutMigration(ctx, m); err != nil {
		return err
	}
	mgr.log.Printf("migration %s paused", id)
	return nil
}

// Resume resumes a paused migration.
func (mgr *Manager) Resume(ctx context.Context, id string) error {
	mgr.mu.Lock()
	am, ok := mgr.active[id]
	mgr.mu.Unlock()
	if !ok {
		return fmt.Errorf("migration %s not running", id)
	}
	m, err := mgr.store.GetMigration(ctx, id)
	if err != nil {
		return err
	}
	if m.Status != model.MigrationPaused {
		return fmt.Errorf("can only resume paused migrations (status=%s)", m.Status)
	}
	select {
	case <-am.pause:
		// already closed
	default:
		close(am.pause)
	}
	m.Status = model.MigrationRunning
	m.PausedAt = time.Time{}
	if err := mgr.store.PutMigration(ctx, m); err != nil {
		return err
	}
	mgr.log.Printf("migration %s resumed", id)
	return nil
}

// Cancel cancels a running or paused migration and triggers rollback.
func (mgr *Manager) Cancel(ctx context.Context, id string) error {
	mgr.mu.Lock()
	am, ok := mgr.active[id]
	mgr.mu.Unlock()
	if !ok {
		return fmt.Errorf("migration %s not running", id)
	}
	am.cancel()
	mgr.log.Printf("migration %s cancelled", id)
	return nil
}

func (mgr *Manager) Get(ctx context.Context, id string) (*model.Migration, error) {
	return mgr.store.GetMigration(ctx, id)
}

func (mgr *Manager) List(ctx context.Context) ([]*model.Migration, error) {
	return mgr.store.ListMigrations(ctx)
}

// run is the main migration goroutine.
func (mgr *Manager) run(ctx context.Context, mig *model.Migration, am *activeMigration) {
	defer func() {
		mgr.mu.Lock()
		delete(mgr.active, mig.ID)
		mgr.mu.Unlock()
	}()

	mig.Status = model.MigrationRunning
	mig.StartedAt = time.Now()
	_ = mgr.store.PutMigration(ctx, mig)

	// Step 1: baseline measurement of source
	srcFiles, srcBytes, err := measurePath(mig.SourcePath)
	if err != nil {
		mgr.fail(ctx, mig, fmt.Errorf("baseline source: %w", err))
		return
	}
	mig.FilesTotal = srcFiles
	mig.BytesTotal = srcBytes
	_ = mgr.store.PutMigration(ctx, mig)

	mgr.log.Printf("migration %s baseline: %d files, %d bytes", mig.ID, srcFiles, srcBytes)

	// Step 2: prepare target directory
	if err := os.MkdirAll(mig.TargetPath, 0755); err != nil {
		mgr.fail(ctx, mig, fmt.Errorf("mkdir target: %w", err))
		return
	}

	// Step 3: rsync passes (incremental, converges)
	const maxPasses = 5
	for pass := 1; pass <= maxPasses; pass++ {
		select {
		case <-ctx.Done():
			mgr.rollback(ctx, mig, "cancelled")
			return
		case <-am.pause:
		}

		if err := mgr.waitWhilePaused(ctx, am); err != nil {
			mgr.rollback(ctx, mig, "cancelled during pause")
			return
		}

		useDelete := pass >= 3 // start deleting on pass 3+
		if err := mgr.rsyncPass(ctx, mig, useDelete); err != nil {
			mgr.fail(ctx, mig, fmt.Errorf("rsync pass %d: %w", pass, err))
			return
		}

		// Update progress after each pass
		tgtFiles, tgtBytes, err := measurePath(mig.TargetPath)
		if err == nil {
			mig.FilesCopied = tgtFiles
			mig.BytesCopied = tgtBytes
			if srcBytes > 0 {
				mig.Progress = int(float64(tgtBytes) / float64(srcBytes) * 100)
				if mig.Progress > 100 {
					mig.Progress = 100
				}
			}
			_ = mgr.store.PutMigration(ctx, mig)
		}

		mgr.log.Printf("migration %s pass %d done: %d/%d files, %d/%d bytes",
			mig.ID, pass, mig.FilesCopied, mig.FilesTotal, mig.BytesCopied, mig.BytesTotal)

		// Check convergence
		if srcFiles > 0 && tgtFiles >= srcFiles-1 && tgtFiles <= srcFiles+1 &&
			(srcBytes == 0 || tgtBytes >= srcBytes-1024) {
			mgr.log.Printf("migration %s converged after pass %d", mig.ID, pass)
			break
		}
	}

	// Step 4: final rsync with --delete
	if err := mgr.waitWhilePaused(ctx, am); err != nil {
		mgr.rollback(ctx, mig, "cancelled before final sync")
		return
	}
	mig.Status = model.MigrationSwitching
	_ = mgr.store.PutMigration(ctx, mig)

	if err := mgr.rsyncPass(ctx, mig, true); err != nil {
		mgr.fail(ctx, mig, fmt.Errorf("final rsync: %w", err))
		return
	}

	// Step 5: atomic switch via symlink
	if err := mgr.atomicSwitch(ctx, mig); err != nil {
		mgr.rollback(ctx, mig, fmt.Sprintf("atomic switch: %v", err))
		return
	}

	// Step 6: verify data integrity
	mig.Status = model.MigrationVerifying
	_ = mgr.store.PutMigration(ctx, mig)

	if err := mgr.verify(ctx, mig); err != nil {
		mgr.rollback(ctx, mig, fmt.Sprintf("verification failed: %v", err))
		return
	}

	// Step 7: update tenant and usage
	if err := mgr.updateTenantAfterMigration(ctx, mig); err != nil {
		mgr.log.Printf("migration %s post-update failed: %v", mig.ID, err)
	}

	// Step 8: mark complete
	mig.Status = model.MigrationCompleted
	mig.Progress = 100
	mig.FinishedAt = time.Now()
	_ = mgr.store.PutMigration(ctx, mig)
	mgr.log.Printf("migration %s completed", mig.ID)

	// Clean up old source after successful migration (keep for a while, or delete)
	// Keep the old data for safety; admin can clean up.
}

func (mgr *Manager) rsyncPass(ctx context.Context, mig *model.Migration, delete bool) error {
	src := mig.SourcePath + string(os.PathSeparator)
	dst := mig.TargetPath
	args := []string{"-aHAX", "--numeric-ids", "--quiet"}
	if delete {
		args = append(args, "--delete")
	}
	args = append(args, src, dst)

	cmd := exec.CommandContext(ctx, "rsync", args...)
	cmd.Stdout = os.Stderr
	cmd.Stderr = os.Stderr
	mgr.log.Printf("rsync %s %s %s", strings.Join(args, " "), src, dst)
	return cmd.Run()
}

// atomicSwitch replaces the tenant's export symlink to point to the new target.
// The tenant's ExportPath is the logical path (e.g., /nfs/exports/tenant-123).
// If it's already a symlink, we atomically replace it.
// If it's a real directory, we first convert it: move it to a backup path,
// create a symlink from the logical path to the new target.
func (mgr *Manager) atomicSwitch(ctx context.Context, mig *model.Migration) error {
	t, err := mgr.store.GetTenant(ctx, mig.TenantID)
	if err != nil {
		return err
	}

	logicalPath := t.ExportPath
	info, err := os.Lstat(logicalPath)
	if err != nil {
		return fmt.Errorf("lstat %s: %w", logicalPath, err)
	}

	tmpLink := logicalPath + ".migtmp"
	_ = os.Remove(tmpLink)

	if err := os.Symlink(mig.TargetPath, tmpLink); err != nil {
		return fmt.Errorf("create temp symlink: %w", err)
	}

	if info.Mode()&os.ModeSymlink != 0 {
		// Already a symlink: atomic rename replacement
		if err := os.Rename(tmpLink, logicalPath); err != nil {
			_ = os.Remove(tmpLink)
			return fmt.Errorf("rename symlink: %w", err)
		}
	} else {
		// Real directory: move old dir to backup, rename symlink into place
		backupPath := logicalPath + ".migration-backup-" + mig.ID
		if err := os.Rename(logicalPath, backupPath); err != nil {
			_ = os.Remove(tmpLink)
			return fmt.Errorf("backup old dir: %w", err)
		}
		if err := os.Rename(tmpLink, logicalPath); err != nil {
			// rollback: restore backup
			_ = os.Remove(tmpLink)
			_ = os.Rename(backupPath, logicalPath)
			return fmt.Errorf("activate new symlink: %w", err)
		}
	}

	// Re-point inotify watch to new target
	mgr.mon.RemoveTenant(t.ID)
	t.ExportPath = mig.TargetPath
	_ = mgr.mon.AddTenant(ctx, t)

	return nil
}

// verify compares file counts and total bytes between source and target.
func (mgr *Manager) verify(ctx context.Context, mig *model.Migration) error {
	srcFiles, srcBytes, err := measurePath(mig.SourcePath)
	if err != nil {
		return fmt.Errorf("measure source: %w", err)
	}
	tgtFiles, tgtBytes, err := measurePath(mig.TargetPath)
	if err != nil {
		return fmt.Errorf("measure target: %w", err)
	}

	mgr.log.Printf("verify migration %s: src(files=%d, bytes=%d) vs tgt(files=%d, bytes=%d)",
		mig.ID, srcFiles, srcBytes, tgtFiles, tgtBytes)

	// Allow small tolerance for in-flight NFS writes during migration
	if absDiff(srcFiles, tgtFiles) > 10 {
		return fmt.Errorf("file count mismatch: src=%d tgt=%d", srcFiles, tgtFiles)
	}
	if absDiff(srcBytes, tgtBytes) > 10*1024*1024 { // 10 MB tolerance
		return fmt.Errorf("byte count mismatch: src=%d tgt=%d", srcBytes, tgtBytes)
	}
	return nil
}

func (mgr *Manager) updateTenantAfterMigration(ctx context.Context, mig *model.Migration) error {
	t, err := mgr.store.GetTenant(ctx, mig.TenantID)
	if err != nil {
		return err
	}
	t.ExportPath = mig.TargetPath
	t.UpdatedAt = time.Now()
	if err := mgr.store.PutTenant(ctx, t); err != nil {
		return err
	}

	tgtFiles, tgtBytes, _ := measurePath(mig.TargetPath)
	u, err := mgr.store.GetUsage(ctx, t.ID)
	if err != nil {
		u = &model.Usage{TenantID: t.ID}
	}
	u.UsedBytes = tgtBytes
	u.UsedFiles = tgtFiles
	u.LastScanTime = time.Now()
	return mgr.store.PutUsage(ctx, u)
}

func (mgr *Manager) fail(ctx context.Context, mig *model.Migration, err error) {
	mgr.log.Printf("migration %s failed: %v", mig.ID, err)
	mig.Status = model.MigrationFailed
	mig.Error = err.Error()
	mig.FinishedAt = time.Now()
	_ = mgr.store.PutMigration(ctx, mig)
}

func (mgr *Manager) rollback(ctx context.Context, mig *model.Migration, reason string) {
	mgr.log.Printf("migration %s rolling back: %s", mig.ID, reason)

	mig.Status = model.MigrationRollback
	mig.Error = reason
	_ = mgr.store.PutMigration(ctx, mig)

	// Clean up target directory
	_ = exec.Command("rm", "-rf", mig.TargetPath).Run()

	// If we already switched the symlink, restore it
	t, err := mgr.store.GetTenant(ctx, mig.TenantID)
	if err == nil {
		// Try to restore from backup if it exists
		backupPath := t.ExportPath + ".migration-backup-" + mig.ID
		if info, err := os.Stat(backupPath); err == nil && info.IsDir() {
			_ = os.Remove(t.ExportPath)
			_ = os.Rename(backupPath, t.ExportPath)
		}
	}

	mig.Status = model.MigrationFailed
	mig.FinishedAt = time.Now()
	mig.RolledBack = true
	_ = mgr.store.PutMigration(ctx, mig)
}

func (mgr *Manager) waitWhilePaused(ctx context.Context, am *activeMigration) error {
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-am.pause:
		// Running
		return nil
	}
}

// measurePath returns file count and total bytes for a directory.
func measurePath(path string) (int64, int64, error) {
	var files int64
	var bytes int64

	// Use find for file count
	if out, err := exec.Command("find", path, "-printf", ".\n").Output(); err == nil {
		files = int64(strings.Count(string(out), "\n"))
	} else {
		// Fallback: walk
		_ = filepath.Walk(path, func(_ string, _ os.FileInfo, _ error) error {
			files++
			return nil
		})
	}

	// Use du for bytes
	if out, err := exec.Command("du", "-sb", path).Output(); err == nil {
		parts := strings.Fields(string(out))
		if len(parts) >= 1 {
			if v, e := strconv.ParseInt(parts[0], 10, 64); e == nil {
				bytes = v
			}
		}
	}

	return files, bytes, nil
}

func absDiff(a, b int64) int64 {
	if a > b {
		return a - b
	}
	return b - a
}
