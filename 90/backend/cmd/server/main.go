package main

import (
	"log"
	"os"

	"github.com/gin-gonic/gin"

	"configvcs/pkg/api"
	ipfspkg "configvcs/pkg/ipfs"
	"configvcs/pkg/store"
	"configvcs/pkg/version"
)

func main() {
	ipfsAddr := getEnv("IPFS_ADDR", "localhost:5001")
	dbPath := getEnv("DB_PATH", "./data/badger")
	port := getEnv("SERVER_PORT", "8080")

	store, err := store.New(dbPath)
	if err != nil {
		log.Fatalf("Failed to open BadgerDB: %v", err)
	}
	defer store.Close()

	ipfsClient := ipfspkg.New(ipfsAddr)

	svc := version.NewService(store, ipfsClient)

	handler := api.NewHandler(svc)

	r := gin.Default()

	r.Use(func(c *gin.Context) {
		c.Header("Access-Control-Allow-Origin", "*")
		c.Header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
		c.Header("Access-Control-Allow-Headers", "Content-Type, Authorization")
		if c.Request.Method == "OPTIONS" {
			c.AbortWithStatus(204)
			return
		}
		c.Next()
	})

	handler.SetupRoutes(r)

	log.Printf("Server starting on port %s...", port)
	log.Printf("IPFS endpoint: %s", ipfsAddr)
	log.Printf("BadgerDB path: %s", dbPath)

	if err := r.Run(":" + port); err != nil {
		log.Fatalf("Failed to start server: %v", err)
	}
}

func getEnv(key, defaultValue string) string {
	if value, exists := os.LookupEnv(key); exists {
		return value
	}
	return defaultValue
}
