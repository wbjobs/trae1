package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"cdn-cache-protector/internal/api"
	"cdn-cache-protector/internal/bloom"
	"cdn-cache-protector/internal/cache"
	"cdn-cache-protector/internal/detector"
	"cdn-cache-protector/internal/handler"
	"cdn-cache-protector/internal/singleflight"
)

func main() {
	redisAddr := getEnv("REDIS_ADDR", "localhost:6379")
	redisPassword := getEnv("REDIS_PASSWORD", "")
	redisDB := 0

	bloomExpectedItems := flag.Uint64("bf-expected-items", 100000, "Expected number of items for Bloom Filter")
	bfAccuracy := flag.Float64("bf-accuracy", 0.01, "Target false positive rate (0.001-0.05, e.g., 0.01 = 1%)")
	hotKeyThreshold := flag.Int64("hotkey-threshold", 1000, "Hot key request threshold per minute")
	serverAddr := flag.String("server-addr", getEnv("SERVER_ADDR", ":8080"), "Server listen address")
	flag.Parse()

	bloomFalsePositiveRate := *bfAccuracy
	if bloomFalsePositiveRate < 0.001 || bloomFalsePositiveRate > 0.05 {
		log.Printf("Warning: bf-accuracy must be between 0.001 and 0.05, using default 0.01")
		bloomFalsePositiveRate = 0.01
	}

	hotKeyWindow := 1 * time.Minute

	bloomFilter := bloom.New(bloomExpectedItems, bloomFalsePositiveRate)

	cacheStore := cache.New(redisAddr, redisPassword, redisDB)
	defer cacheStore.Close()

	ctx := context.Background()
	if err := cacheStore.Ping(ctx); err != nil {
		log.Printf("Warning: Redis connection failed: %v", err)
		log.Println("Running in degraded mode without Redis cache")
	}

	hotKeyDetector := detector.New(hotKeyWindow, *hotKeyThreshold, func(key string) {
		log.Printf("Hot key detected: %s", key)
		bloomFilter.Add(key)
	})
	hotKeyDetector.StartCleanup(10 * time.Second)

	sf := singleflight.New()

	originFunc := func(ctx context.Context, key string) (interface{}, error) {
		time.Sleep(100 * time.Millisecond)
		return map[string]interface{}{
			"key":   key,
			"value": fmt.Sprintf("content-for-%s", key),
			"time":  time.Now().Format(time.RFC3339),
		}, nil
	}

	h := handler.New(bloomFilter, cacheStore, hotKeyDetector, sf, originFunc)

	mux := http.NewServeMux()
	mux.Handle("/cache", h)

	managementAPI := api.NewManagementAPI(bloomFilter, cacheStore, hotKeyDetector, h)
	managementAPI.RegisterRoutes(mux)

	server := &http.Server{
		Addr:         *serverAddr,
		Handler:      mux,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
	}

	go func() {
		log.Printf("Server starting on %s", *serverAddr)
		log.Printf("Configuration:")
		log.Printf("  Bloom Filter expected items: %d", *bloomExpectedItems)
		log.Printf("  Bloom Filter target accuracy: %.2f%%", bloomFalsePositiveRate*100)
		log.Printf("  Hot key threshold: %d req/min", *hotKeyThreshold)
		log.Printf("")
		log.Printf("Endpoints:")
		log.Printf("  GET  /cache?key=xxx           - Cache access endpoint")
		log.Printf("  GET  /api/stats               - System statistics")
		log.Printf("  GET  /api/hotkeys             - Hot key list")
		log.Printf("  GET  /api/bloom/stats         - Bloom filter statistics")
		log.Printf("  GET  /api/bloom/adaptive      - Adaptive Bloom filter statistics")
		log.Printf("  GET  /api/bloom/config        - Get Bloom filter config")
		log.Printf("  PUT  /api/bloom/config?fpr=x  - Update target FPR (0.001-0.05)")
		log.Printf("  POST /api/bloom/rebuild       - Manual trigger Bloom filter rebuild")
		log.Printf("  POST /api/bloom/reset         - Reset bloom filter")
		log.Printf("  GET  /api/shunt/stats         - Response shunt statistics")
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Server failed: %v", err)
		}
	}()

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit

	log.Println("Shutting down server...")

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := server.Shutdown(shutdownCtx); err != nil {
		log.Fatalf("Server forced to shutdown: %v", err)
	}

	log.Println("Server exited")
}

func getEnv(key, defaultValue string) string {
	if value := os.Getenv(key); value != "" {
		return value
	}
	return defaultValue
}
