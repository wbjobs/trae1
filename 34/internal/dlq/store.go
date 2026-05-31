package dlq

import (
	"context"
	"fmt"
	"sort"
	"sync"
	"time"

	"github.com/confluentinc/confluent-kafka-go/v2/kafka"
	jsoniter "github.com/json-iterator/go"
	"msgbridge/internal/config"
	"msgbridge/internal/logger"
)

type DLQMessage struct {
	ID          string    `json:"id"`
	Source      string    `json:"source"`
	SourceTopic string    `json:"source_topic"`
	Target      string    `json:"target"`
	TargetTopic string    `json:"target_topic"`
	Payload     string    `json:"payload"`
	Error       string    `json:"error"`
	Attempts    int       `json:"attempts"`
	CreatedAt   time.Time `json:"created_at"`
	TraceID     string    `json:"trace_id"`
}

type DLQStore struct {
	cfg      config.KafkaConfig
	producer *kafka.Producer
	messages map[string]*DLQMessage
	mu       sync.RWMutex
}

func NewDLQStore(cfg config.KafkaConfig) (*DLQStore, error) {
	brokers := ""
	for i, b := range cfg.Brokers {
		if i > 0 {
			brokers += ","
		}
		brokers += b
	}

	p, err := kafka.NewProducer(&kafka.ConfigMap{
		"bootstrap.servers": brokers,
		"acks":              "all",
		"retries":           3,
	})
	if err != nil {
		return nil, fmt.Errorf("create kafka producer: %w", err)
	}

	return &DLQStore{
		cfg:      cfg,
		producer: p,
		messages: make(map[string]*DLQMessage),
	}, nil
}

func (s *DLQStore) Produce(ctx context.Context, msg *DLQMessage) error {
	data, err := jsoniter.Marshal(msg)
	if err != nil {
		return fmt.Errorf("marshal dlq message: %w", err)
	}

	topic := s.cfg.DLQTopic
	deliveryChan := make(chan kafka.Event, 1)
	err = s.producer.Produce(&kafka.Message{
		TopicPartition: kafka.TopicPartition{
			Topic:     &topic,
			Partition: kafka.PartitionAny,
		},
		Key:   []byte(msg.ID),
		Value: data,
	}, deliveryChan)
	if err != nil {
		return fmt.Errorf("kafka produce: %w", err)
	}

	select {
	case e := <-deliveryChan:
		m := e.(*kafka.Message)
		if m.TopicPartition.Error != nil {
			return m.TopicPartition.Error
		}
		logger.S.Debugf("DLQ message produced: %s", msg.ID)
	case <-ctx.Done():
		return ctx.Err()
	}

	s.mu.Lock()
	s.messages[msg.ID] = msg
	s.mu.Unlock()
	return nil
}

func (s *DLQStore) List(limit, offset int) []*DLQMessage {
	s.mu.RLock()
	defer s.mu.RUnlock()

	var msgs []*DLQMessage
	for _, m := range s.messages {
		msgs = append(msgs, m)
	}
	sort.Slice(msgs, func(i, j int) bool {
		return msgs[i].CreatedAt.After(msgs[j].CreatedAt)
	})
	if offset >= len(msgs) {
		return nil
	}
	end := offset + limit
	if end > len(msgs) {
		end = len(msgs)
	}
	return msgs[offset:end]
}

func (s *DLQStore) Get(id string) (*DLQMessage, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	m, ok := s.messages[id]
	return m, ok
}

func (s *DLQStore) Remove(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	delete(s.messages, id)
}

func (s *DLQStore) Close() {
	if s.producer != nil {
		s.producer.Flush(5000)
		s.producer.Close()
	}
}
