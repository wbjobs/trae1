package retry

import (
	"context"
	"fmt"
	"sync"
	"time"

	"msgbridge/internal/config"
	"msgbridge/internal/dlq"
	"msgbridge/internal/logger"
)

type ReplayFunc func(ctx context.Context, msg *dlq.DLQMessage) error

type RetryManager struct {
	cfg       config.RetryConfig
	store     *dlq.DLQStore
	replayFn  ReplayFunc
	mu        sync.Mutex
	replaying map[string]bool
}

func NewRetryManager(cfg config.RetryConfig, store *dlq.DLQStore) *RetryManager {
	return &RetryManager{
		cfg:       cfg,
		store:     store,
		replaying: make(map[string]bool),
	}
}

func (r *RetryManager) SetReplayFunc(fn ReplayFunc) {
	r.replayFn = fn
}

func (r *RetryManager) Replay(ctx context.Context, msgID string) error {
	r.mu.Lock()
	if r.replaying[msgID] {
		r.mu.Unlock()
		return fmt.Errorf("message %s is already being replayed", msgID)
	}
	r.mu.Unlock()

	msg, ok := r.store.Get(msgID)
	if !ok {
		return fmt.Errorf("message %s not found", msgID)
	}

	return r.doReplay(ctx, msg)
}

func (r *RetryManager) doReplay(ctx context.Context, msg *dlq.DLQMessage) error {
	r.mu.Lock()
	r.replaying[msg.ID] = true
	r.mu.Unlock()
	defer func() {
		r.mu.Lock()
		delete(r.replaying, msg.ID)
		r.mu.Unlock()
	}()

	if r.replayFn == nil {
		return fmt.Errorf("replay function not set")
	}

	var lastErr error
	for attempt := 0; attempt < r.cfg.MaxAttempts; attempt++ {
		backoff := time.Duration(r.cfg.BackoffBaseMs) * (1 << uint(attempt))
		if backoff > time.Duration(r.cfg.BackoffMaxMs)*time.Millisecond {
			backoff = time.Duration(r.cfg.BackoffMaxMs) * time.Millisecond
		}

		if attempt > 0 {
			select {
			case <-time.After(backoff):
			case <-ctx.Done():
				return ctx.Err()
			}
		}

		msg.Attempts = attempt + 1
		if err := r.replayFn(ctx, msg); err != nil {
			logger.S.Warnf("Retry attempt %d for message %s failed: %v", attempt+1, msg.ID, err)
			lastErr = err
			continue
		}

		logger.S.Infof("Message %s replayed successfully on attempt %d", msg.ID, attempt+1)
		r.store.Remove(msg.ID)
		return nil
	}

	logger.S.Errorf("Message %s exhausted retries, keeping in DLQ", msg.ID)
	return fmt.Errorf("all %d retry attempts failed, last error: %v", r.cfg.MaxAttempts, lastErr)
}

func (r *RetryManager) AutoRetry(ctx context.Context, interval time.Duration) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			msgs := r.store.List(100, 0)
			for _, msg := range msgs {
				if msg.Attempts >= r.cfg.MaxAttempts {
					continue
				}
				go func(m *dlq.DLQMessage) {
					if err := r.doReplay(ctx, m); err != nil {
						logger.S.Debugf("Auto retry for %s: %v", m.ID, err)
					}
				}(msg)
			}
		}
	}
}
