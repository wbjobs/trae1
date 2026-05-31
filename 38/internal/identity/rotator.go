package identity

import (
	"log"
	"sync"
	"time"

	"github.com/spiffe/go-spiffe/v2/svid/x509svid"
)

type Rotator struct {
	provider  *WorkloadAPIProvider
	threshold float64
	period    time.Duration
	onRotate  func(*x509svid.SVID)
	stop      chan struct{}
	once      sync.Once
}

func NewRotator(p *WorkloadAPIProvider, threshold float64, period time.Duration, onRotate func(*x509svid.SVID)) *Rotator {
	return &Rotator{provider: p, threshold: threshold, period: period, onRotate: onRotate, stop: make(chan struct{})}
}

func (r *Rotator) Start() { go r.loop() }

func (r *Rotator) Stop() { r.once.Do(func() { close(r.stop) }) }

func (r *Rotator) loop() {
	ticker := time.NewTicker(r.period)
	defer ticker.Stop()
	for {
		select {
		case <-ticker.C:
			r.check()
		case <-r.stop:
			return
		}
	}
}

func (r *Rotator) check() {
	svid, err := r.provider.GetX509SVID()
	if err != nil || svid == nil || len(svid.Certificates) == 0 {
		return
	}
	c := svid.Certificates[0]
	ratio := RemainingLifetime(c.NotBefore, c.NotAfter, time.Now())
	if ratio <= r.threshold {
		log.Printf("[rotator] remaining %.2f%% <= threshold %.2f%%, SPIRE should reissue",
			ratio*100, r.threshold*100)
		if r.onRotate != nil {
			r.onRotate(svid)
		}
	}
}

func (r *Rotator) Force() { r.check() }
