package bridge

import (
	"context"
	"fmt"
	"time"

	"github.com/google/uuid"
	"go.opentelemetry.io/otel/codes"
	"go.opentelemetry.io/otel/trace"
	"msgbridge/internal/audit"
	"msgbridge/internal/config"
	"msgbridge/internal/converter"
	"msgbridge/internal/dlq"
	"msgbridge/internal/logger"
	"msgbridge/internal/mq"
	"msgbridge/internal/tracer"
)

type Bridge struct {
	cfg       *config.Config
	converter *converter.Converter
	nats      *mq.NATSClient
	rocketmq  *mq.RocketMQClient
	tdmq      *mq.TDMQClient
	store     *dlq.DLQStore
	audit     *audit.Store
}

func New(
	cfg *config.Config,
	conv *converter.Converter,
	natsClient *mq.NATSClient,
	rocket *mq.RocketMQClient,
	tdmq *mq.TDMQClient,
	store *dlq.DLQStore,
	auditStore *audit.Store,
) *Bridge {
	return &Bridge{
		cfg:       cfg,
		converter: conv,
		nats:      natsClient,
		rocketmq:  rocket,
		tdmq:      tdmq,
		store:     store,
		audit:     auditStore,
	}
}

func (b *Bridge) Start(ctx context.Context) error {
	for _, m := range b.cfg.Mappings {
		mapping := m

		if err := b.startConsume(ctx, mapping); err != nil {
			return fmt.Errorf("start mapping %s/%s -> %s/%s: %w",
				mapping.Source, mapping.SourceTopic, mapping.Target, mapping.TargetTopic, err)
		}

		subject := b.nats.Subject(mapping.Source, mapping.SourceTopic)
		if _, err := b.nats.Subscribe(subject, func(ctx context.Context, msg *mq.Message) error {
			return b.forward(ctx, mapping, msg)
		}); err != nil {
			return fmt.Errorf("subscribe nats %s: %w", subject, err)
		}

		logger.S.Infof("Mapping enabled: %s/%s -> %s/%s [%s->%s]",
			mapping.Source, mapping.SourceTopic, mapping.Target, mapping.TargetTopic,
			mapping.SourceFormat, mapping.TargetFormat)
	}
	return nil
}

func (b *Bridge) startConsume(ctx context.Context, m config.Mapping) error {
	handler := func(ctx context.Context, msg *mq.Message) error {
		subject := b.nats.Subject(m.Source, m.SourceTopic)
		return b.nats.Publish(ctx, subject, msg.Data, msg.Headers)
	}

	switch m.Source {
	case config.MQTypeRocketMQ:
		return b.rocketmq.Consume(m.SourceTopic, handler)
	case config.MQTypeTDMQ:
		return b.tdmq.Consume(m.SourceTopic, handler)
	default:
		return fmt.Errorf("unknown source: %s", m.Source)
	}
}

func (b *Bridge) forward(ctx context.Context, m config.Mapping, msg *mq.Message) error {
	startTime := time.Now()

	var traceID string
	if msg.Headers != nil {
		traceID = msg.Headers[tracer.TraceIDHeader]
	}
	if traceID == "" {
		traceID = uuid.New().String()
	}

	spanCtx, span := tracer.StartSpan(ctx, "bridge.forward",
		tracer.Attr("bridge.source", string(m.Source)),
		tracer.Attr("bridge.source_topic", m.SourceTopic),
		tracer.Attr("bridge.target", string(m.Target)),
		tracer.Attr("bridge.target_topic", m.TargetTopic),
		tracer.Attr("bridge.trace_id", traceID),
		tracer.AttrInt("bridge.payload_size", int64(len(msg.Data))),
	)
	defer span.End()

	if msg.Headers == nil {
		msg.Headers = make(map[string]string)
	}
	msg.Headers[tracer.TraceIDHeader] = traceID
	msg.Headers[tracer.SpanIDHeader] = tracer.SpanIDFromContext(spanCtx)

	converted, err := b.converter.Convert(msg.Data, m.SourceFormat, m.TargetFormat, m.SourceSchema, m.TargetSchema)
	if err != nil {
		span.SetStatus(codes.Error, "conversion failed")
		span.RecordError(err)
		b.recordAudit(spanCtx, traceID, m, msg, "error", err, startTime, nil)
		return b.sendToDLQ(spanCtx, m, msg, traceID, fmt.Errorf("convert: %w", err))
	}

	var publishErr error
	switch m.Target {
	case config.MQTypeRocketMQ:
		publishErr = b.rocketmq.Publish(spanCtx, m.TargetTopic, converted, msg.Headers)
	case config.MQTypeTDMQ:
		publishErr = b.tdmq.Publish(spanCtx, m.TargetTopic, converted, msg.Headers)
	default:
		publishErr = fmt.Errorf("unknown target: %s", m.Target)
	}

	if publishErr != nil {
		span.SetStatus(codes.Error, "publish failed")
		span.RecordError(publishErr)
		logger.S.Errorf("Forward %s/%s->%s/%s failed: %v", m.Source, m.SourceTopic, m.Target, m.TargetTopic, publishErr)
		b.recordAudit(spanCtx, traceID, m, msg, "error", publishErr, startTime, nil)
		return b.sendToDLQ(spanCtx, m, msg, traceID, publishErr)
	}

	span.SetStatus(codes.Ok, "ok")
	span.SetAttributes(tracer.Attr("bridge.status", "success"))

	b.recordAudit(spanCtx, traceID, m, msg, "success", nil, startTime, converted)

	logger.S.Debugf("Forwarded: %s/%s -> %s/%s (trace=%s)",
		m.Source, m.SourceTopic, m.Target, m.TargetTopic, traceID)
	return nil
}

func (b *Bridge) recordAudit(ctx context.Context, traceID string, m config.Mapping, msg *mq.Message, status string, err error, startTime time.Time, converted []byte) {
	if b.audit == nil {
		return
	}

	spanCtx := trace.SpanContextFromContext(ctx)
	event := &audit.MessageEvent{
		TraceID:     traceID,
		SpanID:      spanCtx.SpanID().String(),
		Source:      string(m.Source),
		SourceTopic: m.SourceTopic,
		Target:      string(m.Target),
		TargetTopic: m.TargetTopic,
		Format:      m.TargetFormat,
		Status:      status,
		DurationMs:  time.Since(startTime).Milliseconds(),
		Timestamp:   startTime,
		PayloadSize: len(msg.Data),
		Headers:     msg.Headers,
	}
	if err != nil {
		event.Error = err.Error()
	}
	if converted != nil {
		event.PayloadSize = len(converted)
	}
	b.audit.Record(event)
}

func (b *Bridge) sendToDLQ(ctx context.Context, m config.Mapping, msg *mq.Message, traceID string, err error) error {
	dlqMsg := &dlq.DLQMessage{
		ID:          uuid.New().String(),
		Source:      string(m.Source),
		SourceTopic: m.SourceTopic,
		Target:      string(m.Target),
		TargetTopic: m.TargetTopic,
		Payload:     string(msg.Data),
		Error:       err.Error(),
		Attempts:    0,
		CreatedAt:   time.Now(),
		TraceID:     traceID,
	}

	if b.audit != nil {
		event := &audit.MessageEvent{
			TraceID:     traceID,
			Source:      string(m.Source),
			SourceTopic: m.SourceTopic,
			Target:      "dlq",
			TargetTopic: b.cfg.Kafka.DLQTopic,
			Format:      m.TargetFormat,
			Status:      "dlq",
			Error:       err.Error(),
			Timestamp:   time.Now(),
			PayloadSize: len(msg.Data),
			Headers:     msg.Headers,
		}
		b.audit.Record(event)
	}

	if dlqErr := b.store.Produce(ctx, dlqMsg); dlqErr != nil {
		logger.S.Errorf("Failed to send to DLQ: %v", dlqErr)
		return dlqErr
	}
	return nil
}

func (b *Bridge) ReplayFromDLQ(ctx context.Context, msg *dlq.DLQMessage) error {
	traceID := msg.TraceID
	if traceID == "" {
		traceID = uuid.New().String()
	}

	_, span := tracer.StartSpan(ctx, "bridge.replay",
		tracer.Attr("bridge.source", msg.Source),
		tracer.Attr("bridge.source_topic", msg.SourceTopic),
		tracer.Attr("bridge.target", msg.Target),
		tracer.Attr("bridge.target_topic", msg.TargetTopic),
		tracer.Attr("bridge.trace_id", traceID),
		tracer.Attr("bridge.replay", "true"),
		tracer.AttrInt("bridge.attempt", int64(msg.Attempts)),
	)
	defer span.End()

	for _, m := range b.cfg.Mappings {
		if string(m.Source) == msg.Source && m.SourceTopic == msg.SourceTopic &&
			string(m.Target) == msg.Target && m.TargetTopic == msg.TargetTopic {

			converted, err := b.converter.Convert([]byte(msg.Payload), m.SourceFormat, m.TargetFormat, m.SourceSchema, m.TargetSchema)
			if err != nil {
				span.SetStatus(codes.Error, "conversion failed")
				span.RecordError(err)
				return fmt.Errorf("convert: %w", err)
			}

			headers := make(map[string]string)
			headers[tracer.TraceIDHeader] = traceID

			switch m.Target {
			case config.MQTypeRocketMQ:
				err = b.rocketmq.Publish(ctx, m.TargetTopic, converted, headers)
			case config.MQTypeTDMQ:
				err = b.tdmq.Publish(ctx, m.TargetTopic, converted, headers)
			}

			if err != nil {
				span.SetStatus(codes.Error, "replay failed")
				span.RecordError(err)
				return err
			}

			span.SetStatus(codes.Ok, "ok")
			return nil
		}
	}
	return fmt.Errorf("no matching mapping for message %s", msg.ID)
}

func (b *Bridge) TDMQStatus() mq.TDMQStatus {
	return b.tdmq.Status()
}
