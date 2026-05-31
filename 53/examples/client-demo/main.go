package main

import (
	"context"
	"fmt"
	"log"
	"time"

	"raftkv/pkg/client"
)

func main() {
	c := client.New([]string{
		"127.0.0.1:8001",
		"127.0.0.1:8002",
		"127.0.0.1:8003",
	}, 2*time.Second)

	ctx := context.Background()

	if err := c.Put(ctx, "foo", "bar"); err != nil {
		log.Fatalf("put: %v", err)
	}
	fmt.Println("put foo=bar OK")

	// Strong read
	v, err := c.Get(ctx, "foo", client.ReadStrong)
	if err != nil {
		log.Fatalf("get strong: %v", err)
	}
	fmt.Println("get strong foo:", v)

	// Local read
	v, err = c.Get(ctx, "foo", client.ReadLocal)
	if err != nil {
		log.Fatalf("get local: %v", err)
	}
	fmt.Println("get local foo:", v)

	// Simulate leader failure. The client will re-probe after 3 timeouts.
	if err := c.Delete(ctx, "foo"); err != nil {
		log.Fatalf("delete: %v", err)
	}
	fmt.Println("delete foo OK")
}
