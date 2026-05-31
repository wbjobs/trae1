package dlq

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"sync"
	"time"

	"github.com/segmentio/kafka-go"
)

type KafkaDLQ struct {
	writer  *kafka.Writer
	topic   string
	brokers []string
	mu      sync.RWMutex
	buffer  []*DeadLetterMessage
	bufferSize int
	flushInterval time.Duration
	done     chan struct{}
}

type DeadLetterMessage struct {
	ID          string            `json:"id"`
	OriginalDoc map[string]interface{} `json:"original_doc"`
	Error       string            `json:"error"`
	StageName   string            `json:"stage_name"`
	PipelineName string           `json:"pipeline_name"`
	Timestamp   time.Time         `json:"timestamp"`
	RetryCount  int               `json:"retry_count"`
}

type DLQConfig struct {
	Brokers       []string `yaml:"brokers"`
	Topic         string   `yaml:"topic"`
	BufferSize    int      `yaml:"buffer_size"`
	FlushInterval int      `yaml:"flush_interval_ms"`
}

func NewKafkaDLQ(cfg DLQConfig) (*KafkaDLQ, error) {
	writer := &kafka.Writer{
		Addr:         kafka.TCP(cfg.brokers...),
		Topic:        cfg.topic,
		Balancer:     &kafka.LeastBytes{},
		BatchSize:    100,
		BatchTimeout: time.Duration(cfg.FlushInterval) * time.Millisecond,
		Async:        true,
	}

	bufferSize := cfg.BufferSize
	if bufferSize <= 0 {
		bufferSize = 1000
	}

	flushInterval := time.Duration(cfg.FlushInterval) * time.Millisecond
	if flushInterval <= 0 {
		flushInterval = 1000 * time.Millisecond
	}

	dlq := &KafkaDLQ{
		writer:  writer,
		topic:   cfg.Topic,
		brokers: cfg.Brokers,
		buffer:  make([]*DeadLetterMessage, 0, bufferSize),
		bufferSize: bufferSize,
		flushInterval: flushInterval,
		done:     make(chan struct{}),
	}

	go dlq.backgroundFlush()

	return dlq, nil
}

func (d *KafkaDLQ) Send(ctx context.Context, event interface{}) error {
	data, err := json.Marshal(event)
	if err != nil {
		return fmt.Errorf("failed to marshal event: %w", err)
	}

	msg := kafka.Message{
		Key:   []byte(event.(interface{ GetID() string }).GetID()),
		Value: data,
	}

	return d.writer.WriteMessages(ctx, msg)
}

func (d *KafkaDLQ) SendBatch(ctx context.Context, events []interface{}) error {
	messages := make([]kafka.Message, 0, len(events))
	for _, event := range events {
		data, err := json.Marshal(event)
		if err != nil {
			log.Printf("Failed to marshal event: %v", err)
			continue
		}
		messages = append(messages, kafka.Message{
			Value: data,
		})
	}

	if len(messages) > 0 {
		return d.writer.WriteMessages(ctx, messages...)
	}
	return nil
}

func (d *KafkaDLQ) backgroundFlush() {
	ticker := time.NewTicker(d.flushInterval)
	defer ticker.Stop()

	for {
		select {
		case <-d.done:
			d.flush()
			return
		case <-ticker.C:
			d.flush()
		}
	}
}

func (d *KafkaDLQ) flush() {
	d.mu.Lock()
	defer d.mu.Unlock()

	if len(d.buffer) == 0 {
		return
	}

	messages := make([]kafka.Message, 0, len(d.buffer))
	for _, msg := range d.buffer {
		data, err := json.Marshal(msg)
		if err != nil {
			log.Printf("Failed to marshal DLQ message: %v", err)
			continue
		}
		messages = append(messages, kafka.Message{
			Key:   []byte(msg.ID),
			Value: data,
		})
	}

	if len(messages) > 0 {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()

		if err := d.writer.WriteMessages(ctx, messages...); err != nil {
			log.Printf("Failed to write DLQ messages: %v", err)
			return
		}
	}

	d.buffer = d.buffer[:0]
}

func (d *KafkaDLQ) Close() error {
	close(d.done)
	return d.writer.Close()
}

type InMemoryDLQ struct {
	messages []*DeadLetterMessage
	mu      sync.RWMutex
}

func NewInMemoryDLQ() *InMemoryDLQ {
	return &InMemoryDLQ{
		messages: make([]*DeadLetterMessage, 0),
	}
}

func (d *InMemoryDLQ) Send(ctx context.Context, event interface{}) error {
	d.mu.Lock()
	defer d.mu.Unlock()

	data, err := json.Marshal(event)
	if err != nil {
		return err
	}

	var msg DeadLetterMessage
	if err := json.Unmarshal(data, &msg); err != nil {
		return err
	}

	d.messages = append(d.messages, &msg)
	return nil
}

func (d *InMemoryDLQ) GetMessages() []*DeadLetterMessage {
	d.mu.RLock()
	defer d.mu.RUnlock()

	result := make([]*DeadLetterMessage, len(d.messages))
	copy(result, d.messages)
	return result
}

func (d *InMemoryDLQ) Clear() {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.messages = d.messages[:0]
}

func (d *InMemoryDLQ) Close() error {
	return nil
}

type NoOpDLQ struct{}

func (d *NoOpDLQ) Send(ctx context.Context, event interface{}) error {
	log.Printf("DLQ event (noop): %+v", event)
	return nil
}

func (d *NoOpDLQ) Close() error {
	return nil
}
