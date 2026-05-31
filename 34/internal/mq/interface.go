package mq

import (
	"context"
)

type Message struct {
	Data    []byte
	Headers map[string]string
}

type MessageHandler func(ctx context.Context, msg *Message) error

type Consumer interface {
	Consume(topic string, handler MessageHandler) error
}

type Producer interface {
	Publish(ctx context.Context, topic string, data []byte, headers map[string]string) error
}

type Client interface {
	Consumer
	Producer
	Close()
}
