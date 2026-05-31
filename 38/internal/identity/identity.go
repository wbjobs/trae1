package identity

import (
	"context"
	"fmt"
	"log"
	"sync"
	"time"

	"github.com/spiffe/go-spiffe/v2/svid/jwtsvid"
	"github.com/spiffe/go-spiffe/v2/svid/x509svid"
	"github.com/spiffe/go-spiffe/v2/workloadapi"
)

type Type string

const (
	TypeX509 Type = "x509"
	TypeJWT  Type = "jwt"
)

type DegradeInfo struct {
	UsingExpired bool
	ExpiredAt    time.Time
	GraceUntil   time.Time
	SPIFFEID     string
}

type Provider interface {
	Start(ctx context.Context) error
	Close() error
	GetX509SVID() (*x509svid.SVID, error)
	GetJWTSVID(audience string) (*jwtsvid.SVID, error)
	WatchX509SVID(ctx context.Context, fn func(*x509svid.SVID))
	AllSVIDs() []SVID
	DegradeInfo() *DegradeInfo
}

type SVID struct {
	Type         Type      `json:"type"`
	SPIFFEID     string    `json:"spiffe_id"`
	SerialNumber string    `json:"serial_number,omitempty"`
	Token        string    `json:"token,omitempty"`
	ExpiresAt    time.Time `json:"expires_at"`
	IssuedAt     time.Time `json:"issued_at"`
	Expired      bool      `json:"expired"`
	GraceUntil   time.Time `json:"grace_until,omitempty"`
}

type WorkloadAPIProvider struct {
	socketPath  string
	cacheDir    string
	gracePeriod time.Duration
	onDegrade   func(info DegradeInfo)

	mu          sync.RWMutex
	x509        *x509svid.SVID
	cached      *x509svid.SVID
	client      *workloadapi.Client
	x509Fn      []func(*x509svid.SVID)
	degrade     *DegradeInfo
	degradeFired bool
}

func NewWorkloadAPIProvider(socketPath string) *WorkloadAPIProvider {
	return &WorkloadAPIProvider{
		socketPath:  socketPath,
		cacheDir:    "./data/svid-cache",
		gracePeriod: DefaultGracePeriod,
	}
}

func (p *WorkloadAPIProvider) WithCacheDir(dir string) *WorkloadAPIProvider {
	p.cacheDir = dir
	return p
}

func (p *WorkloadAPIProvider) WithGracePeriod(d time.Duration) *WorkloadAPIProvider {
	p.gracePeriod = d
	return p
}

func (p *WorkloadAPIProvider) OnDegrade(fn func(info DegradeInfo)) *WorkloadAPIProvider {
	p.onDegrade = fn
	return p
}

func (p *WorkloadAPIProvider) Start(ctx context.Context) error {
	if cached, err := loadX509SVID(p.cacheDir); err == nil {
		p.mu.Lock()
		p.cached = cached
		p.mu.Unlock()
		log.Printf("[identity] loaded SVID from disk cache: %s (expires %s)",
			cached.ID.String(), cached.Certificates[0].NotAfter.Format(time.RFC3339))
	}

	c, err := workloadapi.New(ctx, workloadapi.WithAddr("unix://"+p.socketPath))
	if err != nil {
		log.Printf("[identity] SPIRE workload api unavailable (%v), using cached SVID if available", err)
		p.mu.Lock()
		if p.cached != nil {
			p.x509 = p.cached
			p.checkDegradeLocked()
		}
		p.mu.Unlock()
		return nil
	}
	p.client = c
	return p.watchX509Context(ctx)
}

func (p *WorkloadAPIProvider) watchX509Context(ctx context.Context) error {
	return p.client.WatchX509Context(ctx, workloadapi.X509ContextWatcherFunc(func(_ context.Context, x *workloadapi.X509Context, err error) {
		if err != nil || x == nil || len(x.SVIDs) == 0 {
			return
		}
		svid := x.SVIDs[0]

		p.mu.Lock()
		p.x509 = svid
		p.cached = svid
		p.degrade = nil
		p.degradeFired = false
		fns := append([]func(*x509svid.SVID){}, p.x509Fn...)
		p.mu.Unlock()

		if err := persistX509SVID(p.cacheDir, svid); err != nil {
			log.Printf("[identity] persist cache failed: %v", err)
		} else {
			log.Printf("[identity] SVID refreshed and persisted: %s (expires %s)",
				svid.ID.String(), svid.Certificates[0].NotAfter.Format(time.RFC3339))
		}

		for _, fn := range fns {
			fn(svid)
		}
	}))
}

func (p *WorkloadAPIProvider) WatchX509SVID(_ context.Context, fn func(*x509svid.SVID)) {
	p.mu.Lock()
	p.x509Fn = append(p.x509Fn, fn)
	cur := p.currentSVIDLocked()
	p.mu.Unlock()
	if cur != nil && fn != nil {
		fn(cur)
	}
}

func (p *WorkloadAPIProvider) currentSVIDLocked() *x509svid.SVID {
	if p.x509 != nil {
		return p.x509
	}
	return p.cached
}

func (p *WorkloadAPIProvider) GetX509SVID() (*x509svid.SVID, error) {
	p.mu.Lock()
	defer p.mu.Unlock()

	if p.x509 != nil && isSVIDValid(p.x509) {
		return p.x509, nil
	}

	if p.cached != nil && !isSVIDExpired(p.cached, p.gracePeriod) {
		cert := p.cached.Certificates[0]
		info := DegradeInfo{
			UsingExpired: true,
			ExpiredAt:    cert.NotAfter,
			GraceUntil:   cert.NotAfter.Add(p.gracePeriod),
			SPIFFEID:     p.cached.ID.String(),
		}
		p.degrade = &info
		p.fireDegradeLocked(info)
		return p.cached, nil
	}

	if p.cached != nil {
		return nil, fmt.Errorf("cached SVID expired beyond grace period (expired %s, grace until %s)",
			p.cached.Certificates[0].NotAfter.Format(time.RFC3339),
			p.cached.Certificates[0].NotAfter.Add(p.gracePeriod).Format(time.RFC3339))
	}

	return nil, fmt.Errorf("no x509 svid available (SPIRE unavailable and no cache)")
}

func (p *WorkloadAPIProvider) checkDegradeLocked() {
	if p.x509 == nil && p.cached != nil && !isSVIDExpired(p.cached, p.gracePeriod) {
		cert := p.cached.Certificates[0]
		info := DegradeInfo{
			UsingExpired: true,
			ExpiredAt:    cert.NotAfter,
			GraceUntil:   cert.NotAfter.Add(p.gracePeriod),
			SPIFFEID:     p.cached.ID.String(),
		}
		p.degrade = &info
		p.fireDegradeLocked(info)
	}
}

func (p *WorkloadAPIProvider) fireDegradeLocked(info DegradeInfo) {
	if p.onDegrade == nil || p.degradeFired {
		return
	}
	p.degradeFired = true
	go func() {
		defer func() {
			if r := recover(); r != nil {
				log.Printf("[identity] degrade callback panic: %v", r)
			}
		}()
		p.onDegrade(info)
	}()
}

func (p *WorkloadAPIProvider) DegradeInfo() *DegradeInfo {
	p.mu.RLock()
	defer p.mu.RUnlock()
	if p.degrade == nil {
		return nil
	}
	d := *p.degrade
	return &d
}

func (p *WorkloadAPIProvider) GetJWTSVID(audience string) (*jwtsvid.SVID, error) {
	if p.client == nil {
		return nil, fmt.Errorf("workload api client not started")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	return p.client.FetchJWTSVID(ctx, jwtsvid.Params{Audience: audience})
}

func (p *WorkloadAPIProvider) AllSVIDs() []SVID {
	p.mu.RLock()
	defer p.mu.RUnlock()
	out := make([]SVID, 0, 1)
	svid := p.currentSVIDLocked()
	if svid != nil {
		out = append(out, x509ToSVID(svid, p.gracePeriod))
	}
	return out
}

func (p *WorkloadAPIProvider) Close() error {
	if p.client != nil {
		return p.client.Close()
	}
	return nil
}

func x509ToSVID(s *x509svid.SVID, gracePeriod time.Duration) SVID {
	svid := SVID{Type: TypeX509, SPIFFEID: s.ID.String()}
	if len(s.Certificates) > 0 {
		c := s.Certificates[0]
		svid.ExpiresAt = c.NotAfter
		svid.IssuedAt = c.NotBefore
		svid.SerialNumber = c.SerialNumber.String()
		if time.Now().After(c.NotAfter) {
			svid.Expired = true
			svid.GraceUntil = c.NotAfter.Add(gracePeriod)
		}
	}
	return svid
}
