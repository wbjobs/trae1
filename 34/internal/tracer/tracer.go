package tracer

import (
	"context"
	"fmt"

	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/exporters/jaeger"
	"go.opentelemetry.io/otel/propagation"
	"go.opentelemetry.io/otel/sdk/resource"
	tracesdk "go.opentelemetry.io/otel/sdk/trace"
	semconv "go.opentelemetry.io/otel/semconv/v1.17.0"
	"go.opentelemetry.io/otel/trace"
	"msgbridge/internal/config"
)

const (
	TraceIDHeader = "x-bridge-trace-id"
	SpanIDHeader  = "x-bridge-span-id"
)

var Tracer trace.Tracer

func Init(cfg config.TracingConfig) (*tracesdk.TracerProvider, error) {
	if !cfg.Enabled {
		Tracer = otel.GetTracerProvider().Tracer("msgbridge")
		return nil, nil
	}

	exp, err := jaeger.New(jaeger.WithCollectorEndpoint(jaeger.WithEndpoint(cfg.JaegerURL)))
	if err != nil {
		return nil, fmt.Errorf("create jaeger exporter: %w", err)
	}

	res, err := resource.New(context.Background(),
		resource.WithAttributes(
			semconv.ServiceName(cfg.ServiceName),
			semconv.ServiceVersion("1.0.0"),
		),
	)
	if err != nil {
		return nil, fmt.Errorf("create otel resource: %w", err)
	}

	sampleRate := cfg.SampleRate
	if sampleRate > 1.0 {
		sampleRate = 1.0
	}
	sampler := tracesdk.ParentBased(tracesdk.TraceIDRatioBased(sampleRate))

	tp := tracesdk.NewTracerProvider(
		tracesdk.WithBatcher(exp),
		tracesdk.WithResource(res),
		tracesdk.WithSampler(sampler),
	)

	otel.SetTracerProvider(tp)
	otel.SetTextMapPropagator(propagation.NewCompositeTextMapPropagator(
		propagation.TraceContext{},
		propagation.Baggage{},
	))

	Tracer = tp.Tracer("msgbridge")
	return tp, nil
}

func Shutdown(tp *tracesdk.TracerProvider) {
	if tp != nil {
		_ = tp.Shutdown(context.Background())
	}
}

func StartSpan(ctx context.Context, name string, attrs ...attribute.KeyValue) (context.Context, trace.Span) {
	return Tracer.Start(ctx, name, trace.WithAttributes(attrs...))
}

func SpanFromContext(ctx context.Context) trace.Span {
	return trace.SpanFromContext(ctx)
}

func TraceIDFromContext(ctx context.Context) string {
	spanCtx := trace.SpanContextFromContext(ctx)
	if spanCtx.HasTraceID() {
		return spanCtx.TraceID().String()
	}
	return ""
}

func SpanIDFromContext(ctx context.Context) string {
	spanCtx := trace.SpanContextFromContext(ctx)
	if spanCtx.HasSpanID() {
		return spanCtx.SpanID().String()
	}
	return ""
}

func Attr(key, value string) attribute.KeyValue {
	return attribute.String(key, value)
}

func AttrInt(key string, value int64) attribute.KeyValue {
	return attribute.Int64(key, value)
}
