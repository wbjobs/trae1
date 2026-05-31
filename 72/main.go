package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"
)

func main() {
	log.SetFlags(log.Ldate | log.Ltime | log.Lmicroseconds)
	log.Printf("Starting Stock Indicator Service...")

	cfg := DefaultConfig()

	if cfg.MockMode {
		log.Printf("Mock mode enabled, starting mock WebSocket server...")
		mockSrv := NewMockServer(":8765", cfg.MockStocks, cfg.MockTicksPerStock)
		mockSrv.Start()
		time.Sleep(500 * time.Millisecond)
	}

	redisCli := NewRedisClient(cfg.RedisAddr, cfg.RedisPass, cfg.RedisDB)
	defer redisCli.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	if err := redisCli.Ping(ctx); err != nil {
		log.Fatalf("Failed to connect to Redis: %v", err)
	}
	cancel()
	log.Printf("Connected to Redis at %s", cfg.RedisAddr)

	periodMgr := NewPeriodManager(redisCli)
	initCtx := context.Background()
	defaultPeriods := []int64{60, 300, 900, 3600}
	if err := periodMgr.Init(initCtx, defaultPeriods); err != nil {
		log.Printf("Warning: period manager init error: %v", err)
	}
	log.Printf("Active periods: %v", periodMgr.GetPeriods())

	tickChan := make(chan *Tick, 10000)
	klineChan := make(chan *Tick, 10000)

	wsClient := NewWSClient(cfg.WSUrl, tickChan)
	if err := wsClient.Connect(); err != nil {
		log.Fatalf("Failed to connect to WebSocket: %v", err)
	}
	defer wsClient.Close()

	klineAggregator := NewKLineAggregator(redisCli, periodMgr, klineChan, cfg.BatchSize, cfg.BatchInterval)

	processor := NewProcessor(redisCli, tickChan, klineChan, cfg.BatchSize, cfg.BatchInterval)

	procCtx, procCancel := context.WithCancel(context.Background())
	defer procCancel()

	klineAggregator.Start(procCtx)
	processor.Start(procCtx)

	httpServer := NewHTTPServer(redisCli, processor, klineAggregator, periodMgr, cfg.HTTPAddr)
	go func() {
		log.Printf("HTTP server starting on %s", cfg.HTTPAddr)
		if err := httpServer.Start(); err != nil {
			log.Fatalf("HTTP server error: %v", err)
		}
	}()

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit

	log.Printf("Shutting down...")
	procCancel()
	time.Sleep(1 * time.Second)
	log.Printf("Service stopped.")
}
