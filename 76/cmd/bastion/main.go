package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"bastion/internal/ai"
	"bastion/internal/api"
	"bastion/internal/approval"
	"bastion/internal/audit"
	"bastion/internal/config"
	"bastion/internal/dingtalk"
	"bastion/internal/models"
	sshserver "bastion/internal/ssh"
	"bastion/internal/storage"
)

func main() {
	cfg := config.Load()

	store := models.NewSessionStore()

	minioClient, err := storage.NewMinIOClient(cfg.MinIO)
	if err != nil {
		fmt.Fprintf(os.Stderr, "[FATAL] Failed to create MinIO client: %v\n", err)
		os.Exit(1)
	}

	ctx := context.Background()
	if err := minioClient.EnsureBucket(ctx); err != nil {
		fmt.Fprintf(os.Stderr, "[FATAL] Failed to ensure MinIO bucket: %v\n", err)
		os.Exit(1)
	}

	var ollamaClient *ai.OllamaClient
	if cfg.Ollama.Enabled {
		ollamaClient = ai.NewClient(cfg.Ollama.BaseURL, cfg.Ollama.Model, cfg.Ollama.Timeout)
		fmt.Printf("[AI] Ollama enabled: %s (model: %s)\n", cfg.Ollama.BaseURL, cfg.Ollama.Model)
	}

	var dingtalkClient *dingtalk.Client
	if cfg.DingTalk.Enabled {
		dingtalkClient = dingtalk.NewClient(cfg.DingTalk.AppKey, cfg.DingTalk.AppSecret, cfg.DingTalk.AgentID)
		fmt.Println("[DingTalk] Approval integration enabled")
	}

	approvalEngine := approval.NewEngine(cfg, ollamaClient, dingtalkClient)
	interceptor := approval.NewInterceptor(cfg, approvalEngine)

	detector := audit.NewDetector(cfg)

	sshSrv := sshserver.NewServer(cfg, store)

	sshProxy := sshserver.NewProxy(cfg, store)
	sshProxy.SetInterceptor(interceptor)
	sshSrv.SetProxy(sshProxy)

	sshProxy.OnSessionComplete(func(ctx context.Context, session *models.Session, recordFile string) error {
		fmt.Printf("[Audit] Analyzing session %s with %d commands\n", session.ID, len(session.Commands))
		detector.Analyze(session)
		fmt.Printf("[Audit] Session %s risk level: %s, findings: %d\n",
			session.ID, session.RiskLevel, len(session.RiskFindings))
		return nil
	})

	sshProxy.OnSessionComplete(func(ctx context.Context, session *models.Session, recordFile string) error {
		fmt.Printf("[Storage] Uploading recording for session %s\n", session.ID)
		if session.ObjectKey != "" && recordFile != "" {
			if err := storage.UploadSessionRecording(ctx, minioClient, session.ObjectKey, recordFile); err != nil {
				fmt.Fprintf(os.Stderr, "[Storage] Upload failed: %v\n", err)
				return err
			}
			fmt.Printf("[Storage] Upload complete for session %s\n", session.ID)
		}
		return nil
	})

	apiSrv := api.NewServer(cfg, store, minioClient)

	errCh := make(chan error, 2)

	go func() {
		if err := sshSrv.Start(); err != nil {
			errCh <- fmt.Errorf("SSH server: %w", err)
		}
	}()

	go func() {
		if err := apiSrv.Start(); err != nil {
			errCh <- fmt.Errorf("API server: %w", err)
		}
	}()

	fmt.Println("==============================================")
	fmt.Println("  Bastion SSH Session Recorder")
	fmt.Println("==============================================")
	fmt.Printf("  SSH Listen:    %s\n", cfg.SSH.ListenAddr)
	fmt.Printf("  API Listen:    %s\n", cfg.API.ListenAddr)
	fmt.Printf("  MinIO Endpoint: %s\n", cfg.MinIO.Endpoint)
	fmt.Printf("  MinIO Bucket:   %s\n", cfg.MinIO.BucketName)
	fmt.Printf("  Target Host:    %s:%d\n", cfg.SSH.TargetHost, cfg.SSH.TargetPort)
	if cfg.Ollama.Enabled {
		fmt.Printf("  AI Model:       %s\n", cfg.Ollama.Model)
	}
	if cfg.Approval.Enabled {
		fmt.Printf("  Approval:       enabled (threshold: %d, timeout: %s)\n",
			cfg.Approval.HighRiskThreshold, cfg.Approval.Timeout)
	}
	fmt.Println("==============================================")

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)

	select {
	case sig := <-sigCh:
		fmt.Printf("\n[Shutdown] Received signal: %v\n", sig)
	case err := <-errCh:
		fmt.Fprintf(os.Stderr, "[FATAL] Server error: %v\n", err)
	}

	fmt.Println("[Shutdown] Shutting down servers...")

	shutdownCtx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := sshSrv.Shutdown(shutdownCtx); err != nil {
		fmt.Fprintf(os.Stderr, "[Shutdown] SSH server shutdown error: %v\n", err)
	}

	if err := apiSrv.Shutdown(shutdownCtx); err != nil {
		fmt.Fprintf(os.Stderr, "[Shutdown] API server shutdown error: %v\n", err)
	}

	fmt.Println("[Shutdown] Done.")
}
