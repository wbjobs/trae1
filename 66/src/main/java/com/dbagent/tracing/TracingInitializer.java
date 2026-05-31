package com.dbagent.tracing;

import com.dbagent.config.AgentConfig;
import io.opentelemetry.api.OpenTelemetry;
import io.opentelemetry.api.common.Attributes;
import io.opentelemetry.api.trace.Tracer;
import io.opentelemetry.api.trace.propagation.W3CTraceContextPropagator;
import io.opentelemetry.context.propagation.ContextPropagators;
import io.opentelemetry.context.propagation.TextMapPropagator;
import io.opentelemetry.exporter.otlp.trace.OtlpGrpcSpanExporter;
import io.opentelemetry.sdk.OpenTelemetrySdk;
import io.opentelemetry.sdk.resources.Resource;
import io.opentelemetry.sdk.trace.SdkTracerProvider;
import io.opentelemetry.sdk.trace.export.BatchSpanProcessor;
import io.opentelemetry.sdk.trace.samplers.Sampler;
import io.opentelemetry.semconv.ResourceAttributes;

import java.util.concurrent.TimeUnit;

public class TracingInitializer {

    private static volatile OpenTelemetry openTelemetry;
    private static volatile Tracer tracer;
    private static volatile SdkTracerProvider tracerProvider;

    private TracingInitializer() {
    }

    public static synchronized void initialize(AgentConfig config) {
        if (openTelemetry != null) {
            return;
        }

        try {
            Resource resource = Resource.getDefault()
                    .merge(Resource.create(Attributes.of(
                            ResourceAttributes.SERVICE_NAME, config.getServiceName(),
                            ResourceAttributes.SERVICE_VERSION, "1.0.0"
                    )));

            OtlpGrpcSpanExporter spanExporter = OtlpGrpcSpanExporter.builder()
                    .setEndpoint(config.getOtelExporterEndpoint())
                    .setTimeout(30, TimeUnit.SECONDS)
                    .build();

            Sampler sampler = Sampler.traceIdRatioBased(config.getSampleRate());

            tracerProvider = SdkTracerProvider.builder()
                    .setResource(resource)
                    .setSampler(sampler)
                    .addSpanProcessor(BatchSpanProcessor.builder(spanExporter)
                            .setScheduleDelay(5, TimeUnit.SECONDS)
                            .setMaxExportBatchSize(512)
                            .setQueueSize(2048)
                            .build())
                    .build();

            TextMapPropagator propagator = W3CTraceContextPropagator.getInstance();

            openTelemetry = OpenTelemetrySdk.builder()
                    .setTracerProvider(tracerProvider)
                    .setPropagators(ContextPropagators.create(propagator))
                    .build();

            tracer = openTelemetry.getTracer("com.dbagent", "1.0.0");

            Runtime.getRuntime().addShutdownHook(new Thread(() -> {
                try {
                    if (tracerProvider != null) {
                        tracerProvider.close();
                    }
                } catch (Exception e) {
                    System.err.println("[DB-Tracing-Agent] Error shutting down tracer provider: " + e.getMessage());
                }
            }));

            System.out.println("[DB-Tracing-Agent] OpenTelemetry initialized successfully with endpoint: " + config.getOtelExporterEndpoint());
        } catch (Exception e) {
            System.err.println("[DB-Tracing-Agent] Failed to initialize OpenTelemetry: " + e.getMessage());
            e.printStackTrace();
        }
    }

    public static OpenTelemetry getOpenTelemetry() {
        return openTelemetry;
    }

    public static Tracer getTracer() {
        if (tracer == null) {
            throw new IllegalStateException("TracingInitializer not initialized. Call initialize() first.");
        }
        return tracer;
    }

    public static SdkTracerProvider getTracerProvider() {
        return tracerProvider;
    }

    public static boolean isInitialized() {
        return openTelemetry != null;
    }
}
