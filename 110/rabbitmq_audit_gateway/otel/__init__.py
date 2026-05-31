"""OpenTelemetry instrumentation module"""
import time
import logging
from typing import Optional, Dict, Any
from contextlib import contextmanager

logger = logging.getLogger(__name__)

try:
    from opentelemetry import trace
    from opentelemetry.sdk.trace import TracerProvider
    from opentelemetry.sdk.trace.export import BatchSpanProcessor
    from opentelemetry.sdk.resources import Resource, SERVICE_NAME
    from opentelemetry.exporter.otlp.proto.grpc.trace_exporter import OTLPSpanExporter
    from opentelemetry.trace import Status, StatusCode
    OTEL_AVAILABLE = True
except ImportError:
    OTEL_AVAILABLE = False


class NoOpTracer:
    def start_span(self, name, **kwargs):
        return NoOpSpan()

    @contextmanager
    def start_as_current_span(self, name, **kwargs):
        yield NoOpSpan()


class NoOpSpan:
    def set_attribute(self, key, value):
        pass

    def set_status(self, status):
        pass

    def record_exception(self, exception):
        pass

    def end(self):
        pass


class OtelInstrumentation:
    def __init__(self, config):
        self.config = config
        self._enabled = config.otel.enabled and OTEL_AVAILABLE
        self._tracer: Optional[Any] = None
        self._setup_tracer()

    def _setup_tracer(self) -> None:
        if not self._enabled:
            self._tracer = NoOpTracer()
            return

        try:
            resource = Resource.create({
                SERVICE_NAME: self.config.otel.service_name
            })

            provider = TracerProvider(resource=resource)

            exporter = OTLPSpanExporter(
                endpoint=self.config.otel.endpoint,
                insecure=self.config.otel.insecure
            )

            processor = BatchSpanProcessor(exporter)
            provider.add_span_processor(processor)

            trace.set_tracer_provider(provider)
            self._tracer = trace.get_tracer(self.config.otel.service_name)

            logger.info(f"OpenTelemetry initialized with endpoint: {self.config.otel.endpoint}")
        except Exception as e:
            logger.error(f"Failed to initialize OpenTelemetry: {e}")
            self._tracer = NoOpTracer()

    def get_tracer(self):
        return self._tracer

    @contextmanager
    def span(self, name: str, attributes: Optional[Dict[str, Any]] = None):
        if not self._enabled:
            yield NoOpSpan()
            return

        try:
            with self._tracer.start_as_current_span(name) as span:
                if attributes:
                    for key, value in attributes.items():
                        span.set_attribute(key, value)
                yield span
        except Exception as e:
            logger.error(f"Error in span {name}: {e}")
            yield NoOpSpan()

    def record_message_publish(
        self,
        message_id: str,
        exchange: str,
        routing_key: str,
        body_size: int,
        latency_ms: float
    ) -> None:
        if not self._enabled:
            return

        with self.span("message.publish", {
            "message.id": message_id,
            "message.exchange": exchange,
            "message.routing_key": routing_key,
            "message.body_size": body_size,
            "message.latency_ms": latency_ms
        }) as span:
            span.set_attribute("message.direction", "producer_to_rabbitmq")

    def record_message_consume(
        self,
        message_id: str,
        exchange: str,
        routing_key: str,
        body_size: int,
        latency_ms: float
    ) -> None:
        if not self._enabled:
            return

        with self.span("message.consume", {
            "message.id": message_id,
            "message.exchange": exchange,
            "message.routing_key": routing_key,
            "message.body_size": body_size,
            "message.latency_ms": latency_ms
        }) as span:
            span.set_attribute("message.direction", "rabbitmq_to_consumer")

    def record_interception(
        self,
        message_id: str,
        rule_id: str,
        rule_name: str,
        severity: str
    ) -> None:
        if not self._enabled:
            return

        with self.span("message.intercept", {
            "message.id": message_id,
            "interception.rule_id": rule_id,
            "interception.rule_name": rule_name,
            "interception.severity": severity
        }) as span:
            span.set_attribute("message.intercepted", True)

    def record_sampling(self, message_id: str, sample_rate: float) -> None:
        if not self._enabled:
            return

        with self.span("message.sample", {
            "message.id": message_id,
            "sampling.rate": sample_rate
        }) as span:
            span.set_attribute("message.sampled", True)

    def record_error(self, operation: str, error: Exception) -> None:
        if not self._enabled:
            return

        with self.span(f"error.{operation}") as span:
            span.record_exception(error)
            span.set_status(Status(StatusCode.ERROR))
            span.set_attribute("error.message", str(error))


class PerformanceMonitor:
    def __init__(self):
        self._start_times: Dict[str, float] = {}
        self._latencies: Dict[str, list] = {}
        self._lock = None

    def start_operation(self, operation_id: str) -> None:
        self._start_times[operation_id] = time.time()

    def end_operation(self, operation_id: str) -> Optional[float]:
        if operation_id not in self._start_times:
            return None

        start_time = self._start_times.pop(operation_id)
        latency_ms = (time.time() - start_time) * 1000

        if operation_id not in self._latencies:
            self._latencies[operation_id] = []

        self._latencies[operation_id].append(latency_ms)

        if len(self._latencies[operation_id]) > 1000:
            self._latencies[operation_id] = self._latencies[operation_id][-1000:]

        return latency_ms

    def get_average_latency(self, operation_id: str) -> Optional[float]:
        if operation_id not in self._latencies or not self._latencies[operation_id]:
            return None

        return sum(self._latencies[operation_id]) / len(self._latencies[operation_id])

    def get_percentile_latency(self, operation_id: str, percentile: int) -> Optional[float]:
        if operation_id not in self._latencies or not self._latencies[operation_id]:
            return None

        sorted_latencies = sorted(self._latencies[operation_id])
        index = int(len(sorted_latencies) * percentile / 100)
        return sorted_latencies[min(index, len(sorted_latencies) - 1)]

    def get_stats(self) -> Dict[str, Any]:
        stats = {}
        for operation_id, latencies in self._latencies.items():
            if latencies:
                stats[operation_id] = {
                    "count": len(latencies),
                    "avg_ms": sum(latencies) / len(latencies),
                    "min_ms": min(latencies),
                    "max_ms": max(latencies),
                    "p50_ms": self._get_percentile(latencies, 50),
                    "p95_ms": self._get_percentile(latencies, 95),
                    "p99_ms": self._get_percentile(latencies, 99)
                }
        return stats

    def _get_percentile(self, latencies: list, percentile: int) -> float:
        sorted_latencies = sorted(latencies)
        index = int(len(sorted_latencies) * percentile / 100)
        return sorted_latencies[min(index, len(sorted_latencies) - 1)]
