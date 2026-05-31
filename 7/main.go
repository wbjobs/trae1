package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"api-signature/config"
	"api-signature/handler"
	"api-signature/middleware"
	"api-signature/repository"
	"api-signature/service"

	"github.com/gin-gonic/gin"
)

func main() {
	config.LoadConfig()

	if err := repository.InitRedis(); err != nil {
		log.Fatalf("Failed to initialize Redis: %v", err)
	}
	defer repository.CloseRedis()

	signatureService := service.NewSignatureService()
	clientService := service.NewClientService()
	permissionService := service.NewPermissionService()
	blacklistService := service.NewBlacklistService()
	auditService := service.NewAuditService()
	secretRotationService := service.NewSecretRotationService()
	statsService := service.NewStatsService()

	apiHandler := handler.NewApiHandler(
		signatureService,
		clientService,
		permissionService,
		blacklistService,
		auditService,
		secretRotationService,
		statsService,
	)

	gin.SetMode(gin.ReleaseMode)
	router := gin.New()

	router.Use(gin.Logger())
	router.Use(gin.Recovery())
	router.Use(middleware.AuditMiddleware(auditService))

	router.GET("/health", apiHandler.HealthCheck)

	authGroup := router.Group("/api/v1")
	{
		authGroup.POST("/signature/generate", apiHandler.GenerateSignature)
		authGroup.POST("/signature/verify", apiHandler.VerifyRequest)
	}

	protectedGroup := router.Group("/api/v1")
	protectedGroup.Use(middleware.BlacklistMiddleware(blacklistService))
	protectedGroup.Use(middleware.SignatureMiddleware(signatureService, clientService))
	protectedGroup.Use(middleware.TimestampMiddleware(signatureService))
	protectedGroup.Use(middleware.NonceMiddleware(signatureService))
	protectedGroup.Use(middleware.RateLimitMiddleware(clientService))
	protectedGroup.Use(middleware.EndpointRateLimitMiddleware(clientService))
	{
		readGroup := protectedGroup.Group("")
		readGroup.Use(middleware.PermissionMiddleware(permissionService, clientService, "api:read"))
		{
			readGroup.GET("/data/read", apiHandler.ProtectedRead)
			readGroup.GET("/client/:client_id", apiHandler.GetClientInfo)
			readGroup.GET("/permissions", apiHandler.GetPermissions)
			readGroup.GET("/ratelimit/:client_id", apiHandler.GetRateLimit)
			readGroup.GET("/ratelimit/:client_id/info", apiHandler.GetRateLimitInfo)
			readGroup.GET("/secret/:client_id/history", apiHandler.GetSecretHistory)
		}

		writeGroup := protectedGroup.Group("")
		writeGroup.Use(middleware.PermissionMiddleware(permissionService, clientService, "api:write"))
		{
			writeGroup.POST("/data/write", apiHandler.ProtectedWrite)
			writeGroup.POST("/secret/rotate", apiHandler.RotateSecret)
			writeGroup.POST("/endpoint/limit", apiHandler.SetEndpointLimit)
		}

		adminGroup := protectedGroup.Group("")
		adminGroup.Use(middleware.PermissionMiddleware(permissionService, clientService, "api:admin"))
		{
			adminGroup.GET("/admin/clients", apiHandler.GetAllClients)
			adminGroup.GET("/admin/data", apiHandler.ProtectedAdmin)
			adminGroup.POST("/admin/blacklist", apiHandler.AddBlacklist)
			adminGroup.DELETE("/admin/blacklist/:ip", apiHandler.RemoveBlacklist)
			adminGroup.GET("/admin/ip-status", apiHandler.CheckIPStatus)
			adminGroup.GET("/admin/stats/abnormal", apiHandler.GetAbnormalStats)
			adminGroup.GET("/admin/stats/errors", apiHandler.GetErrorStats)
			adminGroup.GET("/admin/stats/abnormal-ips", apiHandler.GetAbnormalIPs)
			adminGroup.GET("/admin/alerts", apiHandler.GetSecurityAlerts)
			adminGroup.POST("/admin/alerts", apiHandler.CreateSecurityAlert)
		}
	}

	router.NoRoute(apiHandler.NotFound)

	serverPort := config.AppConfig.Server.Port
	srv := &http.Server{
		Addr:         fmt.Sprintf(":%s", serverPort),
		Handler:      router,
		ReadTimeout:  time.Duration(config.AppConfig.Server.ReadTimeout) * time.Second,
		WriteTimeout: time.Duration(config.AppConfig.Server.WriteTimeout) * time.Second,
	}

	go func() {
		log.Printf("Server starting on port %s...", serverPort)
		log.Printf("Signature API Service is running")
		log.Printf("Security Features: Signature Verification, Anti-Replay, Rate Limiting, Secret Rotation, Tamper Detection")
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("Failed to start server: %v", err)
		}
	}()

	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit

	log.Println("Shutting down server...")

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := srv.Shutdown(ctx); err != nil {
		log.Fatalf("Server forced to shutdown: %v", err)
	}

	log.Println("Server exited properly")
}
