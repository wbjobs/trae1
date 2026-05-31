package ssh

import (
	"context"
	"crypto/rand"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"

	"bastion/internal/config"
	"bastion/internal/models"

	gliderssh "github.com/gliderlabs/ssh"
	gossh "golang.org/x/crypto/ssh"
)

type Server struct {
	cfg     *config.Config
	server  *gliderssh.Server
	proxy   *Proxy
	store   *models.SessionStore
}

func NewServer(cfg *config.Config, store *models.SessionStore) *Server {
	return &Server{
		cfg:   cfg,
		store: store,
	}
}

func (s *Server) SetProxy(proxy *Proxy) {
	s.proxy = proxy
}

func (s *Server) Proxy() *Proxy {
	return s.proxy
}

func (s *Server) Start() error {
	hostKey, err := s.loadOrGenerateHostKey()
	if err != nil {
		return fmt.Errorf("load host key: %w", err)
	}

	s.server = &gliderssh.Server{
		Addr:             s.cfg.SSH.ListenAddr,
		Handler:          s.proxy.Handler(),
		HostSigners:      []gossh.Signer{hostKey},
		PasswordHandler:  s.passwordAuth,
		PublicKeyHandler: s.publicKeyAuth,
	}

	fmt.Printf("[SSH] Bastion server listening on %s\n", s.cfg.SSH.ListenAddr)
	return s.server.ListenAndServe()
}

func (s *Server) Shutdown(ctx context.Context) error {
	if s.server != nil {
		return s.server.Shutdown(ctx)
	}
	return nil
}

func (s *Server) passwordAuth(ctx gliderssh.Context, password string) bool {
	return password != ""
}

func (s *Server) publicKeyAuth(ctx gliderssh.Context, key gossh.PublicKey) bool {
	return true
}

func (s *Server) loadOrGenerateHostKey() (gossh.Signer, error) {
	keyFile := s.cfg.SSH.HostKeyFile

	if keyData, err := os.ReadFile(keyFile); err == nil {
		signer, err := gossh.ParsePrivateKey(keyData)
		if err == nil {
			return signer, nil
		}
	}

	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return nil, fmt.Errorf("generate rsa key: %w", err)
	}

	keyDER := x509.MarshalPKCS1PrivateKey(key)
	keyBlock := &pem.Block{
		Type:  "RSA PRIVATE KEY",
		Bytes: keyDER,
	}

	keyPEM := pem.EncodeToMemory(keyBlock)
	if err := os.WriteFile(keyFile, keyPEM, 0600); err != nil {
		return nil, fmt.Errorf("write host key: %w", err)
	}

	return gossh.NewSignerFromKey(key)
}
