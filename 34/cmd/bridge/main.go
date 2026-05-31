package main

import (
	"context"
	"flag"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"msgbridge/internal/api"
	"msgbridge/internal/audit"
	"msgbridge/internal/authz"
	"msgbridge/internal/bridge"
	"msgbridge/internal/config"
	"msgbridge/internal/converter"
	"msgbridge/internal/dlq"
	"msgbridge/internal/logger"
	"msgbridge/internal/mq"
	"msgbridge/internal/retry"
	"msgbridge/internal/tracer"
)

func main() {
	configPath := flag.String("config", "config.yaml", "path to config file")
	flag.Parse()

	cfg, err := config.Load(*configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "load config: %v\n", err)
		os.Exit(1)
	}

	logger.Init(cfg.App.LogLevel)
	defer logger.Sync()

	logger.S.Infof("Starting %s...", cfg.App.Name)

	tp, err := tracer.Init(cfg.Tracing)
	if err != nil {
		logger.S.Errorf("init tracer: %v", err)
	} else {
		defer tracer.Shutdown(tp)
	}

	conv := converter.New()

	natsClient, err := mq.NewNATSClient(cfg.NATS)
	if err != nil {
		logger.S.Fatalf("create NATS client: %v", err)
	}
	defer natsClient.Close()

	rocketClient, err := mq.NewRocketMQClient(cfg.RocketMQ)
	if err != nil {
		logger.S.Fatalf("create RocketMQ client: %v", err)
	}
	defer rocketClient.Close()

	tdmqClient, err := mq.NewTDMQClient(cfg.TDMQ)
	if err != nil {
		logger.S.Fatalf("create TDMQ client: %v", err)
	}
	defer tdmqClient.Close()

	store, err := dlq.NewDLQStore(cfg.Kafka)
	if err != nil {
		logger.S.Fatalf("create DLQ store: %v", err)
	}
	defer store.Close()

	auditStore := audit.NewStore(10000)

	var authzManager *authz.Manager
	if cfg.SVID.Enabled && len(cfg.Etcd.Endpoints) > 0 {
		authzStore, err := authz.NewEtcdStore(cfg.Etcd.Endpoints, 10000)
		if err != nil {
			logger.S.Errorf("create etcd authz store: %v, authorization disabled", err)
		} else {
			defer authzStore.Close()
			svidVerifier := authz.NewSVIDVerifier(cfg.SVID)
			authzManager = authz.NewManager(authzStore, svidVerifier, cfg.App.Name, cfg.SVID.Enabled)
			logger.S.Infof("SVID authorization enabled: service=%s", cfg.App.Name)
		}
	}

	rm := retry.NewRetryManager(cfg.Retry, store)

	b := bridge.New(cfg, conv, natsClient, rocketClient, tdmqClient, store, auditStore)
	rm.SetReplayFunc(b.ReplayFromDLQ)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	if err := b.Start(ctx); err != nil {
		logger.S.Fatalf("start bridge: %v", err)
	}

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		rm.AutoRetry(ctx, 30*time.Second)
	}()

	handler := api.NewHandler(store, rm, auditStore, b.TDMQStatus)
	srv := startHTTPServer(cfg.HTTP.Addr, handler, authzManager)

	logger.S.Infof("%s started successfully (tracing=%v, authz=%v)", cfg.App.Name, cfg.Tracing.Enabled, authzManager != nil && authzManager.IsEnabled())
	logger.S.Infof("Web UI available at http://localhost%s", cfg.HTTP.Addr)

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	logger.S.Info("Shutting down...")
	cancel()

	shutdownCtx, shutdownCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer shutdownCancel()

	if err := srv.Shutdown(shutdownCtx); err != nil {
		logger.S.Errorf("HTTP server shutdown error: %v", err)
	}

	wg.Wait()

	logger.S.Info("Shutdown complete")
}

func startHTTPServer(addr string, handler *api.Handler, authzManager *authz.Manager) *http.Server {
	mux := http.NewServeMux()
	handler.RegisterRoutes(mux)

	if authzManager != nil {
		authzManager.RegisterAdminRoutes(mux)
	}

	mux.Handle("/", http.FileServer(http.Dir("web")))

	srv := &http.Server{
		Addr:         addr,
		Handler:      mux,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 30 * time.Second,
	}

	go func() {
		logger.S.Infof("HTTP server listening on %s", addr)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			logger.S.Fatalf("HTTP server error: %v", err)
		}
	}()

	return srv
}