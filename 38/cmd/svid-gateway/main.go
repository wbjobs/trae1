package main

import (
	"context"
	"crypto/x509"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/spiffe-gateway/svid-gateway/internal/admin"
	"github.com/spiffe-gateway/svid-gateway/internal/audit"
	"github.com/spiffe-gateway/svid-gateway/internal/config"
	"github.com/spiffe-gateway/svid-gateway/internal/identity"
	"github.com/spiffe-gateway/svid-gateway/internal/policy"
	"github.com/spiffe-gateway/svid-gateway/internal/registry"
	"github.com/spiffe-gateway/svid-gateway/internal/store"
	"github.com/spiffe/go-spiffe/v2/svid/x509svid"
)

func main() {
	cfg := config.Load()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	provider := identity.NewWorkloadAPIProvider(cfg.SPIRESocketPath).
		WithCacheDir(cfg.SVIDCacheDir).
		WithGracePeriod(cfg.SVIDGracePeriod)
	if err := provider.Start(ctx); err != nil {
		log.Printf("warn: SPIRE workload api not available (%v), running in stub mode", err)
	}
	defer provider.Close()

	etcdStore, err := store.NewEtcdStore(cfg.ETCDEndpoints, cfg.PolicyKeyPrefix, cfg.AuditKeyPrefix)
	if err != nil {
		log.Fatalf("etcd: %v", err)
	}
	defer etcdStore.Close()

	reg, err := registry.New(cfg.ETCDEndpoints, "/svid-gateway/identities/")
	if err != nil {
		log.Fatalf("registry: %v", err)
	}
	defer reg.Close()

	engine := policy.NewEngine()
	initial, err := etcdStore.List(ctx)
	if err != nil {
		log.Printf("warn: initial policy load: %v", err)
	} else {
		engine.Set(initial)
	}
	if err := etcdStore.Watch(ctx, func(ps []*policy.Policy) {
		engine.Set(ps)
		log.Printf("policy reloaded (%d items)", len(ps))
	}); err != nil {
		log.Printf("warn: policy watch: %v", err)
	}

	auditor := audit.NewLogger(etcdStore)

	provider.OnDegrade(func(info identity.DegradeInfo) {
		log.Printf("[degrade] SVID expired but using cache: %s (expired %s, grace until %s)",
			info.SPIFFEID, info.ExpiredAt.Format(time.RFC3339), info.GraceUntil.Format(time.RFC3339))
		auditor.Log(context.Background(), audit.Entry{
			Action:   "svid.degrade",
			Operator: "system",
			Message:  fmt.Sprintf("SPIFFE ID %s expired at %s, using cached SVID until %s",
				info.SPIFFEID, info.ExpiredAt.Format(time.RFC3339), info.GraceUntil.Format(time.RFC3339)),
		})
	})

	roots := x509.NewCertPool()
	svid, _ := provider.GetX509SVID()
	if svid != nil && len(svid.Certificates) > 1 {
		for i := 1; i < len(svid.Certificates); i++ {
			roots.AddCert(svid.Certificates[i])
		}
	}

	srv := admin.New(etcdStore, engine, provider, reg, auditor, roots)
	r := srv.Router()

	httpSrv := &http.Server{
		Addr:         fmt.Sprintf(":%d", cfg.AdminPort),
		Handler:      r,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 30 * time.Second,
	}

	go func() {
		log.Printf("admin server listening on :%d", cfg.AdminPort)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("admin server: %v", err)
		}
	}()

	rotator := identity.NewRotator(provider, cfg.RotateThreshold, cfg.RotateCheckPeriod, func(s *x509svid.SVID) {
		log.Printf("[rotator] rotation triggered for %s", s.ID.String())
	})
	rotator.Start()
	defer rotator.Stop()

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig

	log.Printf("shutting down")
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer shutdownCancel()
	_ = httpSrv.Shutdown(shutdownCtx)
}
