package mq

import (
	"context"
	"fmt"

	"github.com/apache/rocketmq-client-go/v2"
	"github.com/apache/rocketmq-client-go/v2/consumer"
	"github.com/apache/rocketmq-client-go/v2/primitive"
	"github.com/apache/rocketmq-client-go/v2/producer"
	"msgbridge/internal/config"
	"msgbridge/internal/logger"
	"msgbridge/internal/tracer"
)

type RocketMQClient struct {
	cfg       config.RocketMQConfig
	producer  rocketmq.Producer
	consumers []rocketmq.PushConsumer
}

func NewRocketMQClient(cfg config.RocketMQConfig) (*RocketMQClient, error) {
	p, err := rocketmq.NewProducer(
		producer.WithNsResolver(primitive.NewPassthroughResolver([]string{cfg.NameServer})),
		producer.WithRetry(2),
	)
	if err != nil {
		return nil, fmt.Errorf("create rocketmq producer: %w", err)
	}

	if err := p.Start(); err != nil {
		return nil, fmt.Errorf("start rocketmq producer: %w", err)
	}

	return &RocketMQClient{
		cfg:      cfg,
		producer: p,
	}, nil
}

func (r *RocketMQClient) Consume(topic string, handler MessageHandler) error {
	c, err := rocketmq.NewPushConsumer(
		consumer.WithNsResolver(primitive.NewPassthroughResolver([]string{r.cfg.NameServer})),
		consumer.WithGroupName(r.cfg.Group),
		consumer.WithConsumerModel(consumer.Clustering),
		consumer.WithConsumeFromWhere(consumer.ConsumeFromLastOffset),
	)
	if err != nil {
		return fmt.Errorf("create rocketmq consumer: %w", err)
	}

	err = c.Subscribe(topic, consumer.MessageSelector{}, func(ctx context.Context, msgs ...*primitive.MessageExt) (consumer.ConsumeResult, error) {
		for _, msg := range msgs {
			headers := make(map[string]string)
			if msg.Properties != nil {
				for k, v := range msg.Properties {
					headers[k] = v
				}
			}

			bridgeMsg := &Message{
				Data:    msg.Body,
				Headers: headers,
			}

			if err := handler(ctx, bridgeMsg); err != nil {
				logger.S.Errorf("RocketMQ consume error topic=%s: %v", topic, err)
				return consumer.ConsumeRetryLater, nil
			}
		}
		return consumer.ConsumeSuccess, nil
	})
	if err != nil {
		return fmt.Errorf("rocketmq subscribe: %w", err)
	}

	if err := c.Start(); err != nil {
		return fmt.Errorf("start rocketmq consumer: %w", err)
	}

	r.consumers = append(r.consumers, c)
	return nil
}

func (r *RocketMQClient) Publish(ctx context.Context, topic string, data []byte, headers map[string]string) error {
	msg := &primitive.Message{
		Topic: topic,
		Body:  data,
	}
	if headers != nil {
		msg.WithProperties(headers)
	}

	traceID := tracer.TraceIDFromContext(ctx)
	if traceID != "" {
		msg.WithProperty(tracer.TraceIDHeader, traceID)
	}

	_, err := r.producer.SendSync(ctx, msg)
	return err
}

func (r *RocketMQClient) Close() {
	for _, c := range r.consumers {
		_ = c.Shutdown()
	}
	if r.producer != nil {
		_ = r.producer.Shutdown()
	}
}
