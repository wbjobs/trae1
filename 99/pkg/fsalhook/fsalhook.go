//go:build linux && cgo
// +build linux,cgo

// Package fsalhook provides a C-compatible entry point for nfs-ganesha's
// FSAL (File System Abstraction Layer) to enforce tenant quotas in real-time.
//
// The nfs-ganesha FSAL module loads this shared library and calls
// fsal_check_write before each CREATE/WRITE/MKDIR op. When the quota is
// exceeded, the hook returns ENOSPC so Ganesha propagates it to clients.
//
// Build as a shared library:
//   go build -buildmode=c-shared -o libquotafsal.so ./pkg/fsalhook
//
// Then configure Ganesha's FSAL to load the hook via its FSAL_VFS plugin.
package fsalhook

/*
#include <stdint.h>
#include <errno.h>
*/
import "C"

import (
	"context"
	"os"
	"sync"

	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/enforce"
	"github.com/tenantnfs/quotad/internal/store"
)

var (
	initOnce sync.Once
	enf      *enforce.Enforcer
)

func initEnforcer() {
	cfg := config.Default()
	if v := os.Getenv("QUOTAD_ETCD_ENDPOINTS"); v != "" {
		cfg.EtcdEndpoints = []string{v}
	}
	if v := os.Getenv("QUOTAD_ETCD_PREFIX"); v != "" {
		cfg.EtcdPrefix = v
	}
	s, err := store.New(cfg)
	if err != nil {
		return
	}
	enf = enforce.New(s, s, s)
}

// fsal_check_write is the primary FSAL hook. Returns 0 on success, ENOSPC
// when the tenant is over its effective quota.
//
//export fsal_check_write
func fsal_check_write(tenantID *C.char, addBytes C.int64_t, addFiles C.int64_t) C.int {
	initOnce.Do(initEnforcer)
	if enf == nil {
		return 0
	}
	if err := enf.CheckWrite(context.Background(), C.GoString(tenantID), int64(addBytes), int64(addFiles)); err != nil {
		return C.ENOSPC
	}
	return 0
}

func main() {}
