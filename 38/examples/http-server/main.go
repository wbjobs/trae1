package main

import (
	"context"
	"log"
	"net/http"
	"time"

	"github.com/gin-gonic/gin"

	"github.com/spiffe-gateway/svid-gateway/internal/gateway"
	"github.com/spiffe-gateway/svid-gateway/internal/identity"
	"github.com/spiffe-gateway/svid-gateway/internal/policy"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	jwtV, err := identity.NewJWTValidator(ctx, "/tmp/spire-agent/public/api.sock")
	if err != nil {
		log.Printf("warn: jwt validator: %v", err)
	}

	engine := policy.NewEngine()
	engine.Set([]*policy.Policy{{
		ID: "demo-allow", Name: "Demo Allow",
		Source: "spiffe://example.org/ns/svc/client",
		Destination: "spiffe://example.org/ns/svc/user-api",
		Methods: []string{"GET", "POST"}, Path: "/api/v1/users", PathType: "prefix",
		Effect: policy.EffectAllow, Priority: 100, Enabled: true,
	}})

	gw := gateway.New(jwtV, engine, nil)
	router := gin.New()
	router.Use(gin.Recovery())
	router.Use(gw.HTTPMiddleware("spiffe://example.org/ns/svc/user-api"))

	router.GET("/api/v1/users", func(c *gin.Context) {
		c.JSON(http.StatusOK, gin.H{"users": []gin.H{{"id": 1, "name": "alice"}}})
	})

	srv := &http.Server{Addr: ":8443", Handler: router, ReadTimeout: 10 * time.Second}
	log.Printf("listening on :8443")
	_ = srv.ListenAndServe()
}
