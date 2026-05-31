package backoff

import (
	"context"
	"math"
	"math/rand"
	"sync"
	"time"
)

type ExponentialBackoff struct {
	mu       sync.Mutex
	initial  time.Duration
	max      time.Duration
	attempts int
	active   bool
	nextWait time.Duration
	stopCh   chan struct{}
}

func NewExponential(initial, max time.Duration) *ExponentialBackoff {
	return &ExponentialBackoff{
		initial: initial,
		max:     max,
		stopCh:  make(chan struct{}),
	}
}

func (b *ExponentialBackoff) compute(attempt int) time.Duration {
	backoff := float64(b.initial) * math.Pow(2, float64(attempt))
	jitter := rand.Float64() * float64(b.initial) * 0.5
	wait := time.Duration(backoff + jitter)
	if wait > b.max {
		wait = b.max
	}
	return wait
}

func (b *ExponentialBackoff) Trigger() {
	b.mu.Lock()
	defer b.mu.Unlock()

	if !b.active {
		b.active = true
		b.attempts = 0
	}
	b.attempts++
	b.nextWait = b.compute(b.attempts - 1)
}

func (b *ExponentialBackoff) Wait(ctx context.Context) error {
	for {
		b.mu.Lock()
		if !b.active {
			b.mu.Unlock()
			return nil
		}
		wait := b.nextWait
		b.mu.Unlock()

		select {
		case <-time.After(wait):
			continue
		case <-ctx.Done():
			return ctx.Err()
		case <-b.stopCh:
			return nil
		}
	}
}

func (b *ExponentialBackoff) IsActive() bool {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.active
}

func (b *ExponentialBackoff) NextWait() time.Duration {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.nextWait
}

func (b *ExponentialBackoff) Attempts() int {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.attempts
}

func (b *ExponentialBackoff) Reset() {
	b.mu.Lock()
	defer b.mu.Unlock()
	b.active = false
	b.attempts = 0
	b.nextWait = 0
}

func (b *ExponentialBackoff) Stop() {
	close(b.stopCh)
	b.Reset()
}