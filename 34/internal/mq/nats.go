package mq

import (
	"context"

	"github.com/nats-io/nats.go"
	"msgbridge/internal/config"
	"msgbridge/internal/logger"
)

type NATSClient struct {
	conn    *nats.Conn
	js      nats.JetStreamContext
	prefix  string
	ctx     context.Context
	cancel  context.CancelFunc
}

func NewNATSClient(cfg config.NATSConfig) (*NATSClient, error) {
	nc, err := nats.Connect(cfg.URL,
		nats.MaxReconnects(-1),
		nats.DisconnectErrHandler(func(_ *nats.Conn, err error) {
			logger.S.Errorf("NATS disconnected: %v", err)
		}),
		nats.ReconnectHandler(func(_ *nats.Conn) {
			logger.S.Info("NATS reconnected")
		}),
	)
	if err != nil {
		return nil, err
	}

	js, err := nc.JetStream()
	if err != nil {
		nc.Close()
		return nil, err
	}

	ctx, cancel := context.WithCancel(context.Background())

	return &NATSClient{
		conn:   nc,
		js:     js,
		prefix: cfg.SubjectPrefix,
		ctx:    ctx,
		cancel: cancel,
	}, nil
}

func (c *NATSClient) Subject(source config.MQType, topic string) string {
	return c.prefix + "." + string(source) + "." + topic
}

func (c *NATSClient) Publish(ctx context.Context, subject string, data []byte, headers map[string]string) error {
	var hdr nats.Header
	if len(headers) > 0 {
		hdr = make(nats.Header)
		for k, v := range headers {
			hdr.Set(k, v)
		}
	}

	msg := &nats.Msg{
		Subject: subject,
		Data:    data,
		Header:  hdr,
	}
	_, err := c.js.PublishMsg(msg)
	return err
}

func (c *NATSClient) Subscribe(subject string, handler MessageHandler) (*nats.Subscription, error) {
	return c.js.Subscribe(subject, func(msg *nats.Msg) {
		headers := make(map[string]string)
		if msg.Header != nil {
			for k := range msg.Header {
				headers[k] = msg.Header.Get(k)
			}
		}

		bridgeMsg := &Message{
			Data:    msg.Data,
			Headers: headers,
		}

		if err := handler(c.ctx, bridgeMsg); err != nil {
			logger.S.Errorf("NATS handler error on %s: %v", subject, err)
			_ = msg.Nak()
			return
		}
		_ = msg.Ack()
	})
}

func (c *NATSClient) Close() {
	c.cancel()
	if c.conn != nil {
		c.conn.Close()
	}
}
