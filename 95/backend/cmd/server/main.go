package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"flag"
	"fmt"
	"log"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"time"

	"cloud-gamepad/internal/server"
)

func main() {
	addr := flag.String("addr", ":4443", "listen address")
	certFile := flag.String("cert", "", "TLS cert file (auto-generated if empty)")
	keyFile := flag.String("key", "", "TLS key file (auto-generated if empty)")
	maxPads := flag.Int("max-pads", 4, "maximum simultaneous gamepads")
	reorderBuffer := flag.Int("reorder-buffer", 64, "sliding window size for packet reordering")
	reorderTimeout := flag.Int("reorder-timeout", 20, "reorder buffer timeout in milliseconds")
	predictionWindow := flag.Int("prediction-window", 100, "client prediction window in milliseconds")
	correctionThreshold := flag.Float64("correction-threshold", 0.5, "position difference threshold for correction")
	flag.Parse()

	cert, key := *certFile, *keyFile
	if cert == "" || key == "" {
		c, k, err := autoCert()
		if err != nil {
			log.Fatalf("auto cert: %v", err)
		}
		cert, key = c, k
	}

	srv, err := server.New(server.Config{
		Addr:                *addr,
		CertFile:            cert,
		KeyFile:             key,
		MaxGamepads:         *maxPads,
		ReorderBuffer:       *reorderBuffer,
		ReorderTimeoutMs:    *reorderTimeout,
		PredictionWindowMs:  *predictionWindow,
		CorrectionThreshold: *correctionThreshold,
	})
	if err != nil {
		log.Fatalf("server: %v", err)
	}
	log.Fatal(srv.Run())
}

func autoCert() (string, string, error) {
	dir, err := os.UserCacheDir()
	if err != nil {
		dir = "."
	}
	certDir := filepath.Join(dir, "cloud-gamepad")
	if err := os.MkdirAll(certDir, 0o755); err != nil {
		return "", "", err
	}
	certPath := filepath.Join(certDir, "cert.pem")
	keyPath := filepath.Join(certDir, "key.pem")
	if fileExists(certPath) && fileExists(keyPath) {
		return certPath, keyPath, nil
	}

	priv, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return "", "", err
	}
	tpl := &x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject:      pkix.Name{CommonName: "cloud-gamepad.local"},
		NotBefore:    time.Now().Add(-time.Hour),
		NotAfter:     time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:     x509.KeyUsageDigitalSignature,
		ExtKeyUsage:  []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		IPAddresses:  []net.IP{net.ParseIP("127.0.0.1")},
		DNSNames:     []string{"localhost"},
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &priv.PublicKey, priv)
	if err != nil {
		return "", "", err
	}
	cf, err := os.Create(certPath)
	if err != nil {
		return "", "", err
	}
	defer cf.Close()
	_ = pem.Encode(cf, &pem.Block{Type: "CERTIFICATE", Bytes: der})

	kf, err := os.Create(keyPath)
	if err != nil {
		return "", "", err
	}
	defer kf.Close()
	pk, err := x509.MarshalECPrivateKey(priv)
	if err != nil {
		return "", "", err
	}
	_ = pem.Encode(kf, &pem.Block{Type: "EC PRIVATE KEY", Bytes: pk})

	fmt.Printf("generated cert at %s\n", certDir)
	return certPath, keyPath, nil
}

func fileExists(p string) bool {
	_, err := os.Stat(p)
	return err == nil
}
