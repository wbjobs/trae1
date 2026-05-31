package mq

import (
	"context"
	"fmt"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/apache/pulsar-client-go/pulsar"
	"msgbridge/internal/backoff"
	"msgbridge/internal/config"
	"msgbridge/internal/logger"
	"msgbridge/internal/ratelimit"
	"msgbridge/internal/tracer"
)

type TDMQClient struct {
	cfg             config.TDMQConfig
	client          pulsar.Client
	producers       map[string]pulsar.Producer
	consumers       []pulsar.Consumer
	rateLimit       *ratelimit.SlidingWindowLimiter
	backoff         *backoff.ExponentialBackoff
	rateLimitedCnt  int64
	successCnt      int64
	failCnt         int64
	ctx             context.Context
	cancel          context.CancelFunc
	mu              sync.Mutex
}

func NewTDMQClient(cfg config.TDMQConfig) (*TDMQClient, error) {
	opts := pulsar.ClientOptions{
		URL:               "pulsar://" + cfg.Endpoints,
		OperationTimeout:  30 * time.Second,
		ConnectionTimeout: 30 * time.Second,
	}
	if cfg.AuthToken != "" {
		opts.Authentication = pulsar.NewAuthenticationToken(cfg.AuthToken)
	}

	client, err := pulsar.NewClient(opts)
	if err != nil {
		return nil, fmt.Errorf("create tdmq client: %w", err)
	}

	limiter := ratelimit.NewSlidingWindow(
		cfg.RateLimitPerSec,
		time.Duration(cfg.RateLimitWindowMs)*time.Millisecond,
	)
	ebo := backoff.NewExponential(
		time.Duration(cfg.BackoffInitialMs)*time.Millisecond,
		time.Duration(cfg.BackoffMaxMs)*time.Millisecond,
	)

	ctx, cancel := context.WithCancel(context.Background())

	return &TDMQClient{
		cfg:       cfg,
		client:    client,
		producers: make(map[string]pulsar.Producer),
		rateLimit: limiter,
		backoff:   ebo,
		ctx:       ctx,
		cancel:    cancel,
	}, nil
}

func (t *TDMQClient) fullTopic(topic string) string {
	return fmt.Sprintf("persistent://%s/%s/%s", t.cfg.Tenant, t.cfg.Namespace, topic)
}

func (t *TDMQClient) getOrCreateProducer(topic string) (pulsar.Producer, error) {
	t.mu.Lock()
	defer t.mu.Unlock()

	fullTopic := t.fullTopic(topic)
	if p, ok := t.producers[fullTopic]; ok {
		return p, nil
	}

	p, err := t.client.CreateProducer(pulsar.ProducerOptions{
		Topic:                   fullTopic,
		CompressionType:         pulsar.LZ4,
		BatchingMaxMessages:     1000,
		BatchingMaxPublishDelay: 10 * time.Millisecond,
		SendTimeout:             30 * time.Second,
	})
	if err != nil {
		return nil, fmt.Errorf("create tdmq producer for %s: %w", fullTopic, err)
	}
	t.producers[fullTopic] = p
	return p, nil
}

func isRateLimitErr(err error) bool {
	if err == nil {
		return false
	}
	msg := err.Error()
	return strings.Contains(msg, "429") ||
		strings.Contains(msg, "TooManyRequests") ||
		strings.Contains(msg, "RateLimit") ||
		strings.Contains(msg, "rate limit") ||
		strings.Contains(msg, "ServiceNotReady") ||
		strings.Contains(msg, "ProducerBusy") ||
		strings.Contains(msg, "BrokerMetadata")
}

func (t *TDMQClient) Publish(ctx context.Context, topic string, data []byte, headers map[string]string) error {
	if t.backoff.IsActive() {
		if err := t.backoff.Wait(ctx); err != nil {
			return fmt.Errorf("tdmq publish cancelled during backoff: %w", err)
		}
	}

	t.rateLimit.Wait()

	p, err := t.getOrCreateProducer(topic)
	if err != nil {
		return err
	}

	msg := &pulsar.ProducerMessage{
		Payload: data,
	}

	if len(headers) > 0 {
		msg.Properties = make(map[string]string)
		for k, v := range headers {
			msg.Properties[k] = v
		}
	}

	traceID := tracer.TraceIDFromContext(ctx)
	if traceID != "" {
		if msg.Properties == nil {
			msg.Properties = make(map[string]string)
		}
		msg.Properties[tracer.TraceIDHeader] = traceID
	}

	const maxRetries = 5
	var lastErr error
	for attempt := 0; attempt < maxRetries; attempt++ {
		if attempt > 0 {
			if err := t.backoff.Wait(ctx); err != nil {
				return fmt.Errorf("tdmq publish cancelled during retry backoff: %w", err)
			}
			t.rateLimit.Wait()
		}

		_, err = p.Send(ctx, msg)
		if err == nil {
			if attempt > 0 {
				t.backoff.Reset()
				logger.S.Infof("TDMQ publish recovered after %d retries topic=%s", attempt, topic)
			}
			atomic.AddInt64(&t.successCnt, 1)
			return nil
		}

		lastErr = err
		if isRateLimitErr(err) {
			atomic.AddInt64(&t.rateLimitedCnt, 1)
			logger.S.Warnf("TDMQ rate limited (429) topic=%s, attempt=%d/%d, triggering exponential backoff",
				topic, attempt+1, maxRetries)
			t.backoff.Trigger()
			continue
		}

		atomic.AddInt64(&t.failCnt, 1)
		return fmt.Errorf("tdmq publish topic=%s: %w", topic, err)
	}

	atomic.AddInt64(&t.failCnt, 1)
	return fmt.Errorf("tdmq publish topic=%s: rate limited after %d retries: %w", topic, maxRetries, lastErr)
}

func (t *TDMQClient) Consume(topic string, handler MessageHandler) error {
	fullTopic := t.fullTopic(topic)

	c, err := t.client.Subscribe(pulsar.ConsumerOptions{
		Topic:                       fullTopic,
		SubscriptionName:            t.cfg.Subscription,
		Type:                        pulsar.Shared,
		SubscriptionInitialPosition: pulsar.SubscriptionPositionLatest,
	})
	if err != nil {
		return fmt.Errorf("tdmq subscribe %s: %w", fullTopic, err)
	}

	t.consumers = append(t.consumers, c)

	go func() {
		for {
			msg, err := c.Receive(t.ctx)
			if err != nil {
				if strings.Contains(err.Error(), "closed") || t.ctx.Err() != nil {
					return
				}
				logger.S.Errorf("TDMQ receive error topic=%s: %v", topic, err)
				continue
			}

			headers := make(map[string]string)
			if msg.Properties() != nil {
				for k, v := range msg.Properties() {
					headers[k] = v
				}
			}

			bridgeMsg := &Message{
				Data:    msg.Payload(),
				Headers: headers,
			}

			if err := handler(t.ctx, bridgeMsg); err != nil {
				logger.S.Errorf("TDMQ consume error topic=%s: %v", topic, err)
				_ = c.Nack(msg)
				continue
			}
			_ = c.Ack(msg)
		}
	}()

	return nil
}

func (t *TDMQClient) Close() {
	t.cancel()
	t.backoff.Stop()
	for _, p := range t.producers {
		p.Close()
	}
	for _, c := range t.consumers {
		c.Close()
	}
	if t.client != nil {
		t.client.Close()
	}
}

type TDMQStatus struct {
	BackoffActive   bool   `json:"backoff_active"`
	BackoffAttempts int    `json:"backoff_attempts"`
	BackoffNextWait int64  `json:"backoff_next_wait_ms"`
	RateLimited     int64  `json:"rate_limited_count"`
	SuccessCount    int64  `json:"success_count"`
	FailCount       int64  `json:"fail_count"`
}

func (t *TDMQClient) Status() TDMQStatus {
	return TDMQStatus{
		BackoffActive:   t.backoff.IsActive(),
		BackoffAttempts: t.backoff.Attempts(),
		BackoffNextWait: int64(t.backoff.NextWait() / time.Millisecond),
		RateLimited:     atomic.LoadInt64(&t.rateLimitedCnt),
		SuccessCount:    atomic.LoadInt64(&t.successCnt),
		FailCount:       atomic.LoadInt64(&t.failCnt),
	}
}
