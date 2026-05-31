package ratelimit

import (
	"sync"
	"time"
)

type SlidingWindowLimiter struct {
	mu       sync.Mutex
	window   time.Duration
	maxCount int
	timestamps []time.Time
}

func NewSlidingWindow(maxCount int, window time.Duration) *SlidingWindowLimiter {
	return &SlidingWindowLimiter{
		window:   window,
		maxCount: maxCount,
	}
}

func (l *SlidingWindowLimiter) Allow() bool {
	l.mu.Lock()
	defer l.mu.Unlock()

	now := time.Now()
	cutoff := now.Add(-l.window)

	i := 0
	for i < len(l.timestamps) && l.timestamps[i].Before(cutoff) {
		i++
	}
	if i > 0 {
		l.timestamps = l.timestamps[i:]
	}

	if len(l.timestamps) >= l.maxCount {
		return false
	}

	l.timestamps = append(l.timestamps, now)
	return true
}

func (l *SlidingWindowLimiter) Wait() {
	for {
		if l.Allow() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func (l *SlidingWindowLimiter) Reset() {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.timestamps = l.timestamps[:0]
}
