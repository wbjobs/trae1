package main

import (
	"context"
	"fmt"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/gin-gonic/gin"
	"go.uber.org/zap"

	"github.com/sandbox/executor/internal/aisecurity"
	"github.com/sandbox/executor/internal/config"
	"github.com/sandbox/executor/internal/executor"
	"github.com/sandbox/executor/internal/handler"
	"github.com/sandbox/executor/internal/logger"
	"github.com/sandbox/executor/internal/sandbox"
)

func main() {
	cfg, err := config.Load()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to load configuration: %v\n", err)
		os.Exit(1)
	}

	zapLogger, err := logger.NewLogger(&cfg.Logging)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Failed to initialize logger: %v\n", err)
		os.Exit(1)
	}
	defer zapLogger.Sync()

	zapLogger.Info("Starting sandbox executor service",
		zap.Int("port", cfg.Server.Port),
		zap.Float64("cpu_limit", cfg.Resources.CPULimit),
		zap.Int64("memory_limit", cfg.Resources.MemoryLimit),
		zap.Int("max_concurrent", cfg.Sandbox.MaxConcurrent),
		zap.Duration("default_timeout", cfg.Sandbox.DefaultTimeout),
		zap.Bool("ai_security_enabled", cfg.AISecurity.Enabled),
	)

	sandboxManager := sandbox.NewManager(cfg, zapLogger)
	securityChecker := aisecurity.NewSecurityChecker(&cfg.AISecurity, zapLogger)
	exec := executor.NewExecutor(cfg, zapLogger, sandboxManager, securityChecker)
	h := handler.NewHandler(exec, sandboxManager, securityChecker, zapLogger)

	gin.SetMode(gin.ReleaseMode)
	router := gin.New()

	router.Use(gin.Recovery())
	router.Use(requestLogger(zapLogger))

	h.RegisterRoutes(router)

	server := &http.Server{
		Addr:         fmt.Sprintf(":%d", cfg.Server.Port),
		Handler:      router,
		ReadTimeout:  cfg.Server.ReadTimeout,
		WriteTimeout: cfg.Server.WriteTimeout,
	}

	go func() {
		zapLogger.Info("HTTP server starting", zap.String("addr", server.Addr))
		if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			zapLogger.Fatal("Failed to start HTTP server", zap.Error(err))
		}
	}()

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit

	zapLogger.Info("Shutting down server...")

	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if err := server.Shutdown(ctx); err != nil {
		zapLogger.Fatal("Server forced to shutdown", zap.Error(err))
	}

	zapLogger.Info("Server exited successfully")
}

func requestLogger(logger *zap.Logger) gin.HandlerFunc {
	return func(c *gin.Context) {
		start := time.Now()
		path := c.Request.URL.Path
		query := c.Request.URL.RawQuery

		c.Next()

		end := time.Now()
		latency := end.Sub(start)

		logger.Info("HTTP request",
			zap.Int("status", c.Writer.Status()),
			zap.String("method", c.Request.Method),
			zap.String("path", path),
			zap.String("query", query),
			zap.String("ip", c.ClientIP()),
			zap.String("user-agent", c.Request.UserAgent()),
			zap.Duration("latency", latency),
			zap.String("errors", c.Errors.ByType(gin.ErrorTypePrivate).String()),
		)
	}
}
