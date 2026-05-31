package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"

	"iprep-sync/internal/api"
	"iprep-sync/internal/bgp"
	"iprep-sync/internal/config"
	"iprep-sync/internal/ml"
	"iprep-sync/internal/store"
)

func main() {
	cfgPath := flag.String("config", "config.yaml", "path to config file")
	flag.Parse()

	cfg, err := config.Load(*cfgPath)
	if err != nil {
		log.Fatalf("load config: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		sig := <-sigCh
		log.Printf("received signal %v, shutting down", sig)
		cancel()
	}()

	st := store.New(cfg.Redis)
	if err := st.Client().Ping(ctx).Err(); err != nil {
		log.Fatalf("redis ping: %v", err)
	}
	log.Printf("[main] redis connected to %s", cfg.Redis.Addr)

	var scorer *ml.Scorer
	var fusion *ml.FusionEngine
	var features *ml.FeatureCollector

	if cfg.ML.Enabled {
		scorer, err = ml.NewScorer(cfg.ML)
		if err != nil {
			log.Printf("[main] ML scorer init: %v", err)
		} else {
			log.Printf("[main] ML scorer initialized (model=%s, loaded=%v)", cfg.ML.ModelPath, scorer.Enabled())
		}
		fusion = ml.NewFusionEngine(cfg.ML)
		features = ml.NewFeatureCollector(cfg.ML)
		log.Printf("[main] ML fusion engine ready (bgp_weight=%.2f, ml_weight=%.2f)", cfg.ML.BGPWeight, 1.0-cfg.ML.BGPWeight)
	} else {
		log.Printf("[main] ML scoring disabled")
	}

	mgr := bgp.New(cfg, st)
	go func() {
		if err := mgr.Start(ctx); err != nil {
			log.Printf("[bgp] start error: %v", err)
		}
	}()

	httpSrv := api.New(cfg, st, mgr, scorer, fusion, features)
	if err := httpSrv.Start(ctx); err != nil {
		log.Fatalf("http start: %v", err)
	}

	<-ctx.Done()
	log.Printf("[main] shutdown complete")
}
