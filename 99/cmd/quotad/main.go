package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/tenantnfs/quotad/internal/api"
	"github.com/tenantnfs/quotad/internal/config"
	"github.com/tenantnfs/quotad/internal/lift"
	"github.com/tenantnfs/quotad/internal/migrate"
	"github.com/tenantnfs/quotad/internal/model"
	"github.com/tenantnfs/quotad/internal/monitor"
	"github.com/tenantnfs/quotad/internal/notifier"
	"github.com/tenantnfs/quotad/internal/scanner"
	"github.com/tenantnfs/quotad/internal/store"
)

func main() {
	var (
		syncNow = flag.Bool("sync-now", false, "run an immediate full sync then exit")
		addr    = flag.String("listen", "", "HTTP listen address (overrides config)")
		cfgFile = flag.String("config", "", "path to JSON config file (optional)")
	)
	flag.Parse()

	cfg := loadConfig(*cfgFile)
	if *addr != "" {
		cfg.ListenAddr = *addr
	}

	st, err := store.New(cfg)
	if err != nil {
		log.Fatalf("store init: %v", err)
	}
	defer st.Close()

	mon, err := monitor.New(cfg, st)
	if err != nil {
		log.Fatalf("monitor init: %v", err)
	}
	defer mon.Close()

	sc := scanner.New(cfg, st, mon)
	nf := notifier.New(cfg)
	lm := lift.New(st)
	mg := migrate.New(st, mon)
	srv := api.New(cfg, st, mon, sc, nf, mg)

	if *syncNow {
		log.Println("running immediate full sync")
		if err := sc.FullSync(context.Background()); err != nil {
			log.Fatalf("sync failed: %v", err)
		}
		log.Println("sync complete, exiting")
		return
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Load existing tenants into monitor
	tenants, err := st.ListTenants(ctx)
	if err != nil {
		log.Fatalf("list tenants: %v", err)
	}
	for _, t := range tenants {
		if err := mon.AddTenant(ctx, t); err != nil {
			log.Printf("monitor tenant %s: %v", t.ID, err)
		}
	}

	go mon.Run(ctx)
	go sc.Run(ctx, mon.SyncCh)
	go lm.Run(ctx)
	go runAlertChecker(ctx, cfg, st, nf)

	httpSrv := &http.Server{
		Addr:         cfg.ListenAddr,
		Handler:      srv.Handler(),
		ReadTimeout:  15 * time.Second,
		WriteTimeout: 15 * time.Second,
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	go func() {
		log.Printf("quotad listening on %s", cfg.ListenAddr)
		if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("http server: %v", err)
		}
	}()

	<-sigCh
	log.Println("shutting down")
	cancel()
	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer shutdownCancel()
	_ = httpSrv.Shutdown(shutdownCtx)
}

func loadConfig(path string) *config.Config {
	cfg := config.Default()
	if path == "" {
		return cfg
	}
	f, err := os.Open(path)
	if err != nil {
		log.Printf("config open failed: %v, using defaults", err)
		return cfg
	}
	defer f.Close()
	dec := json.NewDecoder(f)
	if err := dec.Decode(cfg); err != nil {
		log.Printf("config parse failed: %v, using defaults", err)
	}
	return cfg
}

func runAlertChecker(ctx context.Context, cfg *config.Config, st *store.Store, nf *notifier.Notifier) {
	t := time.NewTicker(1 * time.Minute)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			tenants, err := st.ListTenants(ctx)
			if err != nil {
				continue
			}
			for _, tnt := range tenants {
				u, err := st.GetUsage(ctx, tnt.ID)
				if err != nil {
					continue
				}
				if tnt.CapacityBytes > 0 {
					ratio := float64(u.UsedBytes) / float64(tnt.CapacityBytes)
					if ratio >= cfg.AlertThreshold {
						_ = nf.Notify(ctx, &model.AlertEvent{
							TenantID:   tnt.ID,
							Type:       "capacity",
							Message:    fmt.Sprintf("tenant %s usage %.1f%% exceeds threshold %.0f%%", tnt.ID, ratio*100, cfg.AlertThreshold*100),
							UsedBytes:  u.UsedBytes,
							LimitBytes: tnt.CapacityBytes,
							UsedFiles:  u.UsedFiles,
							LimitFiles: tnt.FileCount,
							Ratio:      ratio,
							Time:       time.Now(),
						})
					}
				}
				if tnt.FileCount > 0 {
					ratio := float64(u.UsedFiles) / float64(tnt.FileCount)
					if ratio >= cfg.AlertThreshold {
						_ = nf.Notify(ctx, &model.AlertEvent{
							TenantID:   tnt.ID,
							Type:       "filecount",
							Message:    fmt.Sprintf("tenant %s file usage %.1f%% exceeds threshold %.0f%%", tnt.ID, ratio*100, cfg.AlertThreshold*100),
							UsedBytes:  u.UsedBytes,
							LimitBytes: tnt.CapacityBytes,
							UsedFiles:  u.UsedFiles,
							LimitFiles: tnt.FileCount,
							Ratio:      ratio,
							Time:       time.Now(),
						})
					}
				}
			}
		}
	}
}
