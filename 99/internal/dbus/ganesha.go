//go:build linux
// +build linux

package dbus

import (
	"context"
	"fmt"
	"log"
	"os"

	"github.com/godbus/dbus/v5"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/model"
)

// GaneshaDBus talks to nfs-ganesha via its DBus Admin API.
// It manages exports (add/remove/update) and sends reload signals.
type GaneshaDBus struct {
	cfg *config.Config
	conn *dbus.Conn
	log *log.Logger
}

func New(cfg *config.Config) (*GaneshaDBus, error) {
	conn, err := dbus.SystemBus()
	if err != nil {
		return nil, fmt.Errorf("dbus connect: %w", err)
	}
	return &GaneshaDBus{cfg: cfg, conn: conn, log: log.New(os.Stderr, "[dbus] ", log.LstdFlags)}, nil
}

func (g *GaneshaDBus) Close() {
	if g.conn != nil {
		g.conn.Close()
	}
}

// AddExport creates a new NFS export for the given tenant via Ganesha DBus API.
// This uses the org.ganesha.nfsd.admin interface.
func (g *GaneshaDBus) AddExport(ctx context.Context, t *model.Tenant) error {
	obj := g.conn.Object("org.ganesha.nfsd", "/org/ganesha/nfsd/ExportMgr")
	var reply string
	call := obj.CallWithContext(ctx, "org.ganesha.nfsd.admin.AddExport", 0,
		t.ExportPath,
		buildExportConf(t),
	)
	if call.Err != nil {
		g.log.Printf("AddExport DBus call failed: %v", call.Err)
		return call.Err
	}
	if err := call.Store(&reply); err != nil {
		return err
	}
	g.log.Printf("AddExport reply: %s", reply)
	return nil
}

// RemoveExport removes a tenant's NFS export.
func (g *GaneshaDBus) RemoveExport(ctx context.Context, t *model.Tenant) error {
	obj := g.conn.Object("org.ganesha.nfsd", "/org/ganesha/nfsd/ExportMgr")
	var reply string
	call := obj.CallWithContext(ctx, "org.ganesha.nfsd.admin.RemoveExport", 0,
		t.ExportPath,
	)
	if call.Err != nil {
		g.log.Printf("RemoveExport DBus call failed: %v", call.Err)
		return call.Err
	}
	if err := call.Store(&reply); err != nil {
		return err
	}
	g.log.Printf("RemoveExport reply: %s", reply)
	return nil
}

// ReloadAll forces Ganesha to reload its configuration.
func (g *GaneshaDBus) ReloadAll(ctx context.Context) error {
	obj := g.conn.Object("org.ganesha.nfsd", "/org/ganesha/nfsd/admin")
	var reply string
	call := obj.CallWithContext(ctx, "org.ganesha.nfsd.admin.reload", 0)
	if call.Err != nil {
		g.log.Printf("reload DBus call failed: %v", call.Err)
		return call.Err
	}
	if err := call.Store(&reply); err != nil {
		return err
	}
	g.log.Printf("reload reply: %s", reply)
	return nil
}

func buildExportConf(t *model.Tenant) map[string]interface{} {
	return map[string]interface{}{
		"export_id":  t.ID,
		"path":       t.ExportPath,
		"pseudo":     t.ExportPath,
		"access_type": "RW",
		"fsal":       "VFS",
		"tag":        fmt.Sprintf("tenant-%s", t.ID),
	}
}
