package main

import (
	"log"
	"net/http"
	"os"
	"time"

	"github.com/gorilla/mux"
	"github.com/joho/godotenv"

	"webauthn-auth/internal/handlers"
	"webauthn-auth/internal/middleware"
	"webauthn-auth/internal/risk"
	"webauthn-auth/internal/store"
)

func main() {
	_ = godotenv.Load()

	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}

	rpID := os.Getenv("RP_ID")
	rpOrigin := os.Getenv("RP_ORIGIN")
	rpDisplayName := os.Getenv("RP_DISPLAY_NAME")

	if rpID == "" {
		rpID = "localhost"
	}
	if rpOrigin == "" {
		rpOrigin = "http://localhost:3000"
	}
	if rpDisplayName == "" {
		rpDisplayName = "WebAuthn Demo"
	}

	s := store.New()
	h, err := handlers.New(s)
	if err != nil {
		log.Fatalf("Failed to create handler: %v", err)
	}

	riskConfig := &risk.Config{
		Enabled:        os.Getenv("RISK_ENABLED") != "false",
		TrainInterval:  7 * 24 * time.Hour,
		NumTrees:       100,
		SampleSize:     256,
		ThreatIntelAPI: os.Getenv("THREAT_INTEL_API"),
		ThreatIntelKey: os.Getenv("THREAT_INTEL_KEY"),
	}
	riskEngine := risk.NewRiskEngine(riskConfig)
	recoveryHandler := handlers.NewRecoveryHandler(s, h.GetWebAuthn())
	riskHandler := handlers.NewRiskHandler(riskEngine)

	h.SetRiskEngine(riskEngine)

	r := mux.NewRouter()

	r.HandleFunc("/api/register/begin", h.BeginRegister).Methods("POST")
	r.HandleFunc("/api/register/finish", h.FinishRegister).Methods("POST")
	r.HandleFunc("/api/login/begin", h.BeginLogin).Methods("POST")
	r.HandleFunc("/api/login/finish", h.FinishLogin).Methods("POST")

	r.HandleFunc("/api/recovery/backup", recoveryHandler.BackupCredentials).Methods("POST")
	r.HandleFunc("/api/recovery/decrypt", recoveryHandler.DecryptBackup).Methods("POST")
	r.HandleFunc("/api/recovery/send-code", recoveryHandler.SendVerificationCode).Methods("POST")
	r.HandleFunc("/api/recovery/verify-code", recoveryHandler.VerifyCode).Methods("POST")
	r.HandleFunc("/api/recovery/verify-recovery-code", recoveryHandler.VerifyRecoveryCode).Methods("POST")

	recovery := r.PathPrefix("/api/recovery").Subrouter()
	recovery.Use(middleware.RecoveryAuthMiddleware)
	recovery.HandleFunc("/begin", recoveryHandler.BeginRecovery).Methods("POST")
	recovery.HandleFunc("/finish", recoveryHandler.FinishRecovery).Methods("POST")
	recovery.HandleFunc("/oidc", recoveryHandler.OIDCLogin).Methods("POST")

	r.HandleFunc("/api/risk/check", riskHandler.CheckRisk).Methods("POST")
	r.HandleFunc("/api/risk/retrain", riskHandler.RetrainModel).Methods("POST")
	r.HandleFunc("/api/risk/stats", riskHandler.GetStats).Methods("GET")

	api := r.PathPrefix("/api").Subrouter()
	api.Use(middleware.AuthMiddleware)
	api.HandleFunc("/user/profile", h.GetUserProfile).Methods("GET")
	api.HandleFunc("/user/credentials", h.GetCredentials).Methods("GET")
	api.HandleFunc("/user/credentials/{id}", h.DeleteCredential).Methods("DELETE")
	api.HandleFunc("/risk/history", riskHandler.GetRiskHistory).Methods("GET")
	api.HandleFunc("/risk/config", riskHandler.UpdateConfig).Methods("PUT")

	r.Use(corsMiddleware)

	go func() {
		ticker := time.NewTicker(1 * time.Hour)
		defer ticker.Stop()
		for range ticker.C {
			if riskEngine.ShouldRetrain() {
				log.Println("Retraining risk model...")
				if err := riskEngine.Retrain(); err != nil {
					log.Printf("Risk model retrain failed: %v", err)
				} else {
					log.Println("Risk model retrained successfully")
				}
			}
		}
	}()

	log.Printf("🚀 Server starting on port %s", port)
	log.Printf("🔐 WebAuthn RP ID: %s", rpID)
	log.Printf("🔗 WebAuthn RP Origin: %s", rpOrigin)
	log.Printf("🛡️ Risk Engine: %v", riskConfig.Enabled)
	log.Printf("📊 Threat Intel: %v", riskConfig.ThreatIntelAPI != "")
	log.Printf("🔄 Auto-train interval: %v", riskConfig.TrainInterval)

	if err := http.ListenAndServe(":"+port, r); err != nil {
		log.Fatalf("Server failed: %v", err)
	}
}

func corsMiddleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		origin := r.Header.Get("Origin")
		if origin == "" {
			origin = "http://localhost:3000"
		}
		w.Header().Set("Access-Control-Allow-Origin", origin)
		w.Header().Set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		w.Header().Set("Access-Control-Allow-Headers", "Content-Type, Authorization")
		w.Header().Set("Access-Control-Allow-Credentials", "true")
		w.Header().Set("Access-Control-Max-Age", "86400")

		if r.Method == "OPTIONS" {
			w.WriteHeader(http.StatusOK)
			return
		}

		next.ServeHTTP(w, r)
	})
}
