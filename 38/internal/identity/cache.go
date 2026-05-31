package identity

import (
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"os"
	"path/filepath"
	"time"

	"github.com/spiffe/go-spiffe/v2/spiffeid"
	"github.com/spiffe/go-spiffe/v2/svid/x509svid"
)

const (
	DefaultGracePeriod   = 24 * time.Hour
	DefaultCacheFileName = "svid-cache.json"
)

type cacheFile struct {
	SPIFFEID    string   `json:"spiffe_id"`
	CertPEMs    []string `json:"cert_pems"`
	KeyPEM      string   `json:"key_pem"`
	ExpiresAt   time.Time `json:"expires_at"`
	IssuedAt    time.Time `json:"issued_at"`
}

func persistX509SVID(cacheDir string, svid *x509svid.SVID) error {
	if svid == nil || len(svid.Certificates) == 0 {
		return fmt.Errorf("nil or empty svid")
	}
	if err := os.MkdirAll(cacheDir, 0700); err != nil {
		return fmt.Errorf("mkdir cache dir: %w", err)
	}
	var certPEMs []string
	for _, cert := range svid.Certificates {
		certPEMs = append(certPEMs, string(pem.EncodeToMemory(&pem.Block{
			Type:  "CERTIFICATE",
			Bytes: cert.Raw,
		})))
	}
	keyDER, err := x509.MarshalPKCS8PrivateKey(svid.PrivateKey)
	if err != nil {
		return fmt.Errorf("marshal private key: %w", err)
	}
	keyPEM := string(pem.EncodeToMemory(&pem.Block{
		Type:  "PRIVATE KEY",
		Bytes: keyDER,
	}))

	cf := cacheFile{
		SPIFFEID:  svid.ID.String(),
		CertPEMs:  certPEMs,
		KeyPEM:    keyPEM,
		ExpiresAt: svid.Certificates[0].NotAfter,
		IssuedAt:  svid.Certificates[0].NotBefore,
	}
	raw, err := json.Marshal(cf)
	if err != nil {
		return err
	}
	tmpPath := filepath.Join(cacheDir, DefaultCacheFileName+".tmp")
	finalPath := filepath.Join(cacheDir, DefaultCacheFileName)
	if err := os.WriteFile(tmpPath, raw, 0600); err != nil {
		return err
	}
	return os.Rename(tmpPath, finalPath)
}

func loadX509SVID(cacheDir string) (*x509svid.SVID, error) {
	raw, err := os.ReadFile(filepath.Join(cacheDir, DefaultCacheFileName))
	if err != nil {
		return nil, fmt.Errorf("read cache file: %w", err)
	}
	var cf cacheFile
	if err := json.Unmarshal(raw, &cf); err != nil {
		return nil, fmt.Errorf("unmarshal cache: %w", err)
	}
	var certs []*x509.Certificate
	for _, pemStr := range cf.CertPEMs {
		block, _ := pem.Decode([]byte(pemStr))
		if block == nil {
			return nil, fmt.Errorf("decode cert PEM failed")
		}
		cert, err := x509.ParseCertificate(block.Bytes)
		if err != nil {
			return nil, fmt.Errorf("parse cert: %w", err)
		}
		certs = append(certs, cert)
	}
	keyBlock, _ := pem.Decode([]byte(cf.KeyPEM))
	if keyBlock == nil {
		return nil, fmt.Errorf("decode key PEM failed")
	}
	key, err := x509.ParsePKCS8PrivateKey(keyBlock.Bytes)
	if err != nil {
		return nil, fmt.Errorf("parse key: %w", err)
	}
	id, err := spiffeid.FromString(cf.SPIFFEID)
	if err != nil {
		return nil, fmt.Errorf("parse spiffe id: %w", err)
	}
	return &x509svid.SVID{
		ID:           id,
		Certificates: certs,
		PrivateKey:   key,
	}, nil
}

func isSVIDExpired(svid *x509svid.SVID, gracePeriod time.Duration) bool {
	if svid == nil || len(svid.Certificates) == 0 {
		return true
	}
	expiry := svid.Certificates[0].NotAfter
	return time.Now().After(expiry.Add(gracePeriod))
}

func isSVIDValid(svid *x509svid.SVID) bool {
	if svid == nil || len(svid.Certificates) == 0 {
		return false
	}
	return time.Now().Before(svid.Certificates[0].NotAfter)
}
