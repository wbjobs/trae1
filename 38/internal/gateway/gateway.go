package gateway

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"net/http"
	"strings"

	"github.com/gin-gonic/gin"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/peer"
	"google.golang.org/grpc/status"

	"github.com/spiffe-gateway/svid-gateway/internal/policy"
)

type Verifier interface {
	VerifyJWT(token string) (spiffeID string, err error)
}

type Gateway struct {
	verifier   Verifier
	engine     *policy.Engine
	trustRoots *x509.CertPool
}

func New(v Verifier, engine *policy.Engine, trustRoots *x509.CertPool) *Gateway {
	return &Gateway{verifier: v, engine: engine, trustRoots: trustRoots}
}

func (g *Gateway) Engine() *policy.Engine { return g.engine }

func extractSPIFFEFromCert(cert *x509.Certificate) (string, error) {
	for _, uri := range cert.URIs {
		if uri.Scheme == "spiffe" {
			return uri.String(), nil
		}
	}
	return "", fmt.Errorf("no SPIFFE ID found in certificate")
}

func (g *Gateway) AuthenticateHTTP(r *http.Request) (string, error) {
	if r.TLS != nil && len(r.TLS.PeerCertificates) > 0 {
		cert := r.TLS.PeerCertificates[0]
		roots := g.trustRoots
		if roots == nil {
			roots = x509.NewCertPool()
		}
		opts := x509.VerifyOptions{
			Roots:     roots,
			KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
		}
		if _, err := cert.Verify(opts); err != nil {
			return "", fmt.Errorf("x509 verify: %w", err)
		}
		return extractSPIFFEFromCert(cert)
	}
	auth := r.Header.Get("Authorization")
	if strings.HasPrefix(auth, "Bearer ") {
		token := strings.TrimSpace(strings.TrimPrefix(auth, "Bearer "))
		if token == "" {
			return "", fmt.Errorf("empty bearer token")
		}
		return g.verifier.VerifyJWT(token)
	}
	return "", fmt.Errorf("no client certificate or bearer token provided")
}

func (g *Gateway) HTTPMiddleware(destinationSPIFFE string) gin.HandlerFunc {
	return func(c *gin.Context) {
		source, err := g.AuthenticateHTTP(c.Request)
		if err != nil {
			c.AbortWithStatusJSON(http.StatusUnauthorized, gin.H{"error": "authentication failed: " + err.Error()})
			return
		}
		decision := g.engine.Evaluate(policy.Request{
			Source:      source,
			Destination: destinationSPIFFE,
			Method:      c.Request.Method,
			Path:        c.Request.URL.Path,
		})
		if !decision.Allowed {
			c.AbortWithStatusJSON(http.StatusForbidden, gin.H{"error": "access denied", "reason": decision.Reason})
			return
		}
		c.Set("spiffe_source", source)
		c.Set("spiffe_destination", destinationSPIFFE)
		c.Set("policy_id", decision.PolicyID)
		c.Next()
	}
}

func (g *Gateway) GRPCUnaryInterceptor(destinationSPIFFE string) grpc.UnaryServerInterceptor {
	return func(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
		source, err := g.authenticateGRPC(ctx)
		if err != nil {
			return nil, status.Error(codes.Unauthenticated, err.Error())
		}
		decision := g.engine.Evaluate(policy.Request{
			Source:      source,
			Destination: destinationSPIFFE,
			Method:      "POST",
			Path:        "/" + info.FullMethod,
		})
		if !decision.Allowed {
			return nil, status.Errorf(codes.PermissionDenied, "access denied: %s", decision.Reason)
		}
		newCtx := context.WithValue(ctx, spiffeSourceKey{}, source)
		newCtx = context.WithValue(newCtx, policyIDKey{}, decision.PolicyID)
		return handler(newCtx, req)
	}
}

func (g *Gateway) GRPCStreamInterceptor(destinationSPIFFE string) grpc.StreamServerInterceptor {
	return func(srv interface{}, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
		source, err := g.authenticateGRPC(ss.Context())
		if err != nil {
			return status.Error(codes.Unauthenticated, err.Error())
		}
		decision := g.engine.Evaluate(policy.Request{
			Source:      source,
			Destination: destinationSPIFFE,
			Method:      "POST",
			Path:        "/" + info.FullMethod,
		})
		if !decision.Allowed {
			return status.Errorf(codes.PermissionDenied, "access denied: %s", decision.Reason)
		}
		return handler(srv, ss)
	}
}

type spiffeSourceKey struct{}
type policyIDKey struct{}

func (g *Gateway) authenticateGRPC(ctx context.Context) (string, error) {
	if p, ok := peer.FromContext(ctx); ok {
		if tlsInfo, ok := p.AuthInfo.(credentials.TLSInfo); ok && len(tlsInfo.State.PeerCertificates) > 0 {
			cert := tlsInfo.State.PeerCertificates[0]
			roots := g.trustRoots
			if roots == nil {
				roots = x509.NewCertPool()
			}
			opts := x509.VerifyOptions{
				Roots:     roots,
				KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageClientAuth},
			}
			if _, err := cert.Verify(opts); err != nil {
				return "", fmt.Errorf("x509 verify: %w", err)
			}
			return extractSPIFFEFromCert(cert)
		}
	}
	md, _ := metadata.FromIncomingContext(ctx)
	if auth := md.Get("authorization"); len(auth) > 0 {
		parts := strings.SplitN(auth[0], " ", 2)
		if len(parts) == 2 && strings.EqualFold(parts[0], "Bearer") {
			return g.verifier.VerifyJWT(strings.TrimSpace(parts[1]))
		}
	}
	return "", fmt.Errorf("no client identity provided")
}

func MakeClientTLSConfig(cert *x509.Certificate, key interface{}, roots *x509.CertPool) *tls.Config {
	return &tls.Config{
		Certificates: []tls.Certificate{{
			Certificate: [][]byte{cert.Raw},
			PrivateKey:  key,
		}},
		RootCAs:    roots,
		MinVersion: tls.VersionTLS12,
	}
}
