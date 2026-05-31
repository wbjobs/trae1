package main

import (
	"context"
	"fmt"
	"io"
	"log"
	"net/http"
	"time"

	"github.com/spiffe/go-spiffe/v2/workloadapi"
)

func main() {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	source, err := workloadapi.NewX509Source(ctx,
		workloadapi.WithAddr("unix:///tmp/spire-agent/public/api.sock"),
	)
	if err != nil {
		log.Fatalf("x509 source: %v", err)
	}
	defer source.Close()

	tlsConfig, err := workloadapi.TLSClientConfig(ctx, source)
	if err != nil {
		log.Fatalf("tls config: %v", err)
	}

	client := &http.Client{
		Transport: &http.Transport{TLSClientConfig: tlsConfig},
		Timeout:   5 * time.Second,
	}
	resp, err := client.Get("https://user-api.example.internal:8443/api/v1/users")
	if err != nil {
		log.Fatalf("request: %v", err)
	}
	defer resp.Body.Close()
	body, _ := io.ReadAll(resp.Body)
	fmt.Println(string(body))
}
