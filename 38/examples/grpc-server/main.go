package main

import (
	"context"
	"log"
	"net"

	"google.golang.org/grpc"
	"google.golang.org/grpc/reflection"

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
	gw := gateway.New(jwtV, engine, nil)

	lis, err := net.Listen("tcp", ":9443")
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	srv := grpc.NewServer(
		grpc.UnaryInterceptor(gw.GRPCUnaryInterceptor("spiffe://example.org/ns/svc/user-api")),
		grpc.StreamInterceptor(gw.GRPCStreamInterceptor("spiffe://example.org/ns/svc/user-api")),
	)
	reflection.Register(srv)
	log.Printf("gRPC listening on :9443")
	if err := srv.Serve(lis); err != nil {
		log.Fatalf("serve: %v", err)
	}
}
