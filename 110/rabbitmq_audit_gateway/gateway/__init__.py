"""Core RabbitMQ message gateway"""
import uuid
import time
import logging
import threading
from typing import Optional, Dict, Any, Callable, List
from dataclasses import dataclass

try:
    import pika
    from pika.exceptions import AMQPConnectionError, AMQPChannelError
    PIKA_AVAILABLE = True
except ImportError:
    PIKA_AVAILABLE = False

import socket

from .audit import AuditRecord, AuditAction, AuditManager, AuditStats, RefCountedBody
from .interception import InterceptionEngine
from .sampling import MessageSampler, SamplingResult
from .sinks import create_sinks
from .ha import HighAvailabilityManager, NodeState
from .otel import OtelInstrumentation, PerformanceMonitor
from .virus_scan import (
    VirusScanEngine, ScanResult, ScanMode,
    ClamAVScanner, YARAScanner, QuarantineMessage
)


logger = logging.getLogger(__name__)


@dataclass
class GatewayMetrics:
    messages_processed: int = 0
    messages_published: int = 0
    messages_consumed: int = 0
    messages_intercepted: int = 0
    messages_dropped: int = 0
    total_bytes: int = 0
    avg_latency_ms: float = 0.0
    p99_latency_ms: float = 0.0


class MessageGateway:
    def __init__(self, config):
        if not PIKA_AVAILABLE:
            raise ImportError("pika is not installed. Install with: pip install pika")

        self.config = config
        self.gateway_id = str(uuid.uuid4())

        self._connection: Optional[pika.BlockingConnection] = None
        self._publish_channel: Optional[pika.channel.Channel] = None
        self._consume_channel: Optional[pika.channel.Channel] = None
        self._running = False
        self._lock = threading.Lock()

        self._sinks = create_sinks(config)
        self._stats = AuditStats()
        self._audit_manager = AuditManager(config, self._sinks, self._stats)

        sampling_config = config.sampling
        self._sampler = MessageSampler(sampling_config)

        self._interception_engine = InterceptionEngine(config.interception)

        self._ha_manager = HighAvailabilityManager(config.ha, self.gateway_id)

        self._otel = OtelInstrumentation(config)

        self._perf_monitor = PerformanceMonitor()

        self._publish_callbacks: List[Callable] = []
        self._consume_callbacks: List[Callable] = []
        self._intercept_callbacks: List[Callable] = []
        self._virus_alert_callbacks: List[Callable] = []

        self._ha_manager.register_active_callback(self._on_become_active)
        self._ha_manager.register_standby_callback(self._on_become_standby)

        self._producer_ip: Optional[str] = None
        self._consumer_ip: Optional[str] = None

        self._virus_scan_engine = self._init_virus_scan_engine()

    def _init_virus_scan_engine(self) -> Optional[VirusScanEngine]:
        """初始化病毒扫描引擎"""
        virus_config = getattr(self.config, 'virus_scan', None)
        if not virus_config or not virus_config.enabled:
            return None

        scanners = []

        if virus_config.clamav and virus_config.clamav.enabled:
            try:
                clamav_scanner = ClamAVScanner(
                    socket_path=virus_config.clamav.socket_path,
                    host=virus_config.clamav.host,
                    port=virus_config.clamav.port
                )
                scanners.append(clamav_scanner)
                logger.info("ClamAV scanner initialized")
            except Exception as e:
                logger.warning(f"Failed to init ClamAV scanner: {e}")

        if virus_config.yara and virus_config.yara.enabled:
            try:
                yara_scanner = YARAScanner(rules_dir=virus_config.yara.rules_dir)
                scanners.append(yara_scanner)
                logger.info("YARA scanner initialized")
            except Exception as e:
                logger.warning(f"Failed to init YARA scanner: {e}")

        if not scanners:
            logger.warning("Virus scan enabled but no scanners available")
            return None

        scan_mode = ScanMode.SYNC if virus_config.scan_mode == "sync" else ScanMode.ASYNC

        engine = VirusScanEngine(
            enabled=True,
            scan_mode=scan_mode,
            scanners=scanners,
            scan_timeout=virus_config.scan_timeout,
            quarantine_exchange=virus_config.quarantine_exchange,
            auto_quarantine=virus_config.auto_quarantine,
            notify_on_infection=virus_config.notify_on_infection,
            max_threads=virus_config.max_threads,
            max_body_size=virus_config.max_body_size,
            min_attachment_size=virus_config.min_attachment_size,
            skip_large_messages=virus_config.skip_large_messages
        )

        engine.add_notify_callback(self._on_virus_detected)

        logger.info(f"Virus scan engine initialized with {len(scanners)} scanners")
        return engine

    def _on_virus_detected(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        result: ScanResult
    ) -> None:
        """病毒检测回调"""
        logger.warning(f"Virus detected in message {message_id}: {result.virus_name}")

        for callback in self._virus_alert_callbacks:
            try:
                callback(message_id, body, headers, result)
            except Exception as e:
                logger.error(f"Virus alert callback error: {e}")

        self._publish_quarantine_message(message_id, body, headers, result)

    def _publish_quarantine_message(
        self,
        message_id: str,
        body: bytes,
        headers: Dict[str, Any],
        result: ScanResult
    ) -> None:
        """发布隔离消息到隔离Exchange"""
        if not self._virus_scan_engine:
            return

        virus_config = getattr(self.config, 'virus_scan', None)
        if not virus_config:
            return

        try:
            channel = self._get_publish_channel()
            if not channel:
                return

            quarantine_headers = {
                **headers,
                'x-original-message-id': message_id,
                'x-virus-name': result.virus_name,
                'x-scanner': result.scanner,
                'x-quarantine-reason': 'virus_detected'
            }

            properties = pika.BasicProperties(
                delivery_mode=2,
                content_type='application/json',
                message_id=str(uuid.uuid4()),
                headers=quarantine_headers
            )

            channel.basic_publish(
                exchange=virus_config.quarantine_exchange,
                routing_key=f"quarantine.{message_id}",
                body=body,
                properties=properties
            )

            logger.info(f"Quarantined message {message_id} to {virus_config.quarantine_exchange}")

        except Exception as e:
            logger.error(f"Failed to publish quarantine message: {e}")

    def _on_become_active(self) -> None:
        logger.info("Gateway became active")
        self._ensure_connection()

    def _on_become_standby(self) -> None:
        logger.info("Gateway became standby")
        self._close_connection()

    def _get_local_ip(self) -> str:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            ip = s.getsockname()[0]
            s.close()
            return ip
        except Exception:
            return "127.0.0.1"

    def _ensure_connection(self) -> bool:
        if self._connection and self._connection.is_open:
            return True

        try:
            credentials = pika.PlainCredentials(
                self.config.gateway.username,
                self.config.gateway.password
            )
            parameters = pika.ConnectionParameters(
                host=self.config.gateway.host,
                port=self.config.gateway.port,
                virtual_host=self.config.gateway.vhost,
                credentials=credentials,
                heartbeat=self.config.gateway.heartbeat,
                connection_attempts=3,
                retry_delay=5
            )

            self._connection = pika.BlockingConnection(parameters)
            self._producer_ip = self._get_local_ip()

            logger.info("Connected to RabbitMQ")
            return True
        except Exception as e:
            logger.error(f"Failed to connect to RabbitMQ: {e}")
            return False

    def _close_connection(self) -> None:
        try:
            if self._publish_channel and self._publish_channel.is_open:
                self._publish_channel.close()
            if self._consume_channel and self._consume_channel.is_open:
                self._consume_channel.close()
            if self._connection and self._connection.is_open:
                self._connection.close()
        except Exception as e:
            logger.error(f"Error closing connection: {e}")
        finally:
            self._publish_channel = None
            self._consume_channel = None
            self._connection = None

    def _get_publish_channel(self) -> Optional[pika.channel.Channel]:
        if not self._ensure_connection():
            return None

        if not self._publish_channel or not self._publish_channel.is_open:
            self._publish_channel = self._connection.channel()

        return self._publish_channel

    def _get_consume_channel(self) -> Optional[pika.channel.Channel]:
        if not self._ensure_connection():
            return None

        if not self._consume_channel or not self._consume_channel.is_open:
            self._consume_channel = self._connection.channel()
            self._consume_channel.basic_qos(prefetch_count=10)

        return self._consume_channel

    def _create_audit_record(
        self,
        channel,
        method,
        properties,
        body: bytes,
        direction: str,
        action: AuditAction
    ) -> AuditRecord:
        message_id = (properties.message_id or str(uuid.uuid4())) if properties else str(uuid.uuid4())
        exchange = method.exchange if method else ""
        routing_key = method.routing_key if method else ""

        headers = dict(properties.headers) if properties and properties.headers else {}

        body_size = len(body) if body else 0

        record = AuditRecord(
            message_id=message_id,
            exchange=exchange,
            routing_key=routing_key,
            timestamp=time.time(),
            producer_ip=self._producer_ip,
            consumer_ip=self._consumer_ip,
            message_body_size=body_size,
            direction=direction,
            action=action.value,
            headers=headers,
            body_preview=body[:1024].decode('utf-8', errors='ignore') if body and len(body) <= 1024 else None
        )

        return record

    def publish(
        self,
        exchange: str,
        routing_key: str,
        body: bytes,
        headers: Optional[Dict[str, Any]] = None,
        properties: Optional[Dict[str, Any]] = None
    ) -> bool:
        start_time = time.time()

        if not self._ha_manager.is_active():
            logger.warning("Gateway is not active, cannot publish")
            return False

        message_id = str(uuid.uuid4())
        headers = headers or {}
        headers['x-gateway-id'] = self.gateway_id

        body_size = len(body) if body else 0

        # 使用should_block_bytes，内部只提取前4KB和后4KB进行检查
        should_block, rule_id, rule_name = self._interception_engine.should_block_bytes(body, headers)

        if should_block:
            self._handle_interception(message_id, exchange, routing_key, body, headers, rule_id, rule_name, start_time)
            return False

        # 使用带body_size参数的should_sample，支持大消息自动降采样
        sample_result = self._sampler.should_sample(message_id, body_size=body_size)

        try:
            channel = self._get_publish_channel()
            if not channel:
                return False

            pika_properties = pika.BasicProperties(
                delivery_mode=2,
                content_type='application/json',
                message_id=message_id,
                headers=headers,
                **(properties or {})
            )

            channel.basic_publish(
                exchange=exchange,
                routing_key=routing_key,
                body=body,
                properties=pika_properties
            )

            latency_ms = (time.time() - start_time) * 1000

            # 使用create_audit_record创建审计记录，自动处理大消息
            record = self._audit_manager.create_audit_record(
                message_id=message_id,
                exchange=exchange,
                routing_key=routing_key,
                producer_ip=self._producer_ip,
                consumer_ip=None,
                body=body,
                ref_body=None,
                direction="producer_to_rabbitmq",
                action=AuditAction.PUBLISH,
                headers=headers,
                sampled=sample_result.should_sample,
                latency_ms=latency_ms
            )

            self._audit_manager.record(record)
            self._otel.record_message_publish(
                message_id, exchange, routing_key,
                body_size, latency_ms
            )

            if sample_result.should_sample:
                self._otel.record_sampling(message_id, sample_result.rate)

            # 异步病毒扫描（不阻塞消息传递）
            if self._virus_scan_engine and self._virus_scan_engine.enabled:
                self._virus_scan_engine.scan_async(message_id, body, headers)

            for callback in self._publish_callbacks:
                try:
                    callback(message_id, exchange, routing_key, body, headers)
                except Exception as e:
                    logger.error(f"Publish callback error: {e}")

            return True

        except Exception as e:
            logger.error(f"Failed to publish message: {e}")
            self._otel.record_error("publish", e)
            return False

    def _handle_interception(
        self,
        message_id: str,
        exchange: str,
        routing_key: str,
        body: bytes,
        headers: Dict[str, Any],
        rule_id: str,
        rule_name: str,
        start_time: Optional[float] = None
    ) -> None:
        latency_ms = (time.time() - start_time) * 1000 if start_time else 0

        record = self._audit_manager.create_audit_record(
            message_id=message_id,
            exchange=exchange,
            routing_key=routing_key,
            producer_ip=self._producer_ip,
            consumer_ip=None,
            body=body,
            ref_body=None,
            direction="producer_to_rabbitmq",
            action=AuditAction.INTERCEPT,
            headers=headers,
            intercepted=True,
            interception_rule_id=rule_id,
            interception_reason=rule_name,
            latency_ms=latency_ms
        )

        self._audit_manager.record(record)
        self._otel.record_interception(message_id, rule_id, rule_name, "high")

        for callback in self._intercept_callbacks:
            try:
                callback(message_id, exchange, routing_key, body, headers, rule_id, rule_name)
            except Exception as e:
                logger.error(f"Intercept callback error: {e}")

        logger.warning(f"Message {message_id} intercepted by rule {rule_id}: {rule_name}")

    def consume(
        self,
        queue: str,
        callback: Callable[[str, bytes, Dict], None],
        auto_ack: bool = False
    ) -> None:
        if not self._ha_manager.is_active():
            logger.warning("Gateway is not active, cannot consume")
            return

        try:
            channel = self._get_consume_channel()
            if not channel:
                return

            def on_message(ch, method, properties, body):
                start_time = time.time()
                message_id = (properties.message_id or str(uuid.uuid4())) if properties else str(uuid.uuid4())
                headers = dict(properties.headers) if properties and properties.headers else {}

                body_size = len(body) if body else 0

                # 使用should_block_bytes，内部只提取前4KB和后4KB进行检查
                should_block, rule_id, rule_name = self._interception_engine.should_block_bytes(body, headers)

                if should_block:
                    if not auto_ack:
                        ch.basic_nack(delivery_tag=method.delivery_tag, requeue=False)
                    self._handle_message_interception(message_id, queue, body, headers, rule_id, rule_name, start_time)
                    return

                # 使用带body_size参数的should_sample，支持大消息自动降采样
                sample_result = self._sampler.should_sample(message_id, body_size=body_size)

                record = self._audit_manager.create_audit_record(
                    message_id=message_id,
                    exchange="",
                    routing_key=queue,
                    producer_ip=None,
                    consumer_ip=self._consumer_ip,
                    body=body,
                    ref_body=None,
                    direction="rabbitmq_to_consumer",
                    action=AuditAction.CONSUME,
                    headers=headers,
                    sampled=sample_result.should_sample,
                    latency_ms=(time.time() - start_time) * 1000
                )

                self._audit_manager.record(record)

                # 异步病毒扫描（不阻塞消息传递）
                if self._virus_scan_engine and self._virus_scan_engine.enabled:
                    self._virus_scan_engine.scan_async(message_id, body, headers)

                try:
                    callback(message_id, body, headers)
                    if not auto_ack:
                        ch.basic_ack(delivery_tag=method.delivery_tag)
                except Exception as e:
                    logger.error(f"Consume callback error: {e}")
                    if not auto_ack:
                        ch.basic_nack(delivery_tag=method.delivery_tag, requeue=True)

                for cb in self._consume_callbacks:
                    try:
                        cb(message_id, queue, body, headers)
                    except Exception as e:
                        logger.error(f"Consume callback error: {e}")

            channel.basic_consume(queue=queue, on_message_callback=on_message, auto_ack=auto_ack)
            channel.start_consuming()

        except Exception as e:
            logger.error(f"Failed to consume from queue {queue}: {e}")
            self._otel.record_error("consume", e)

    def _handle_message_interception(
        self,
        message_id: str,
        queue: str,
        body: bytes,
        headers: Dict[str, Any],
        rule_id: str,
        rule_name: str,
        start_time: float
    ) -> None:
        latency_ms = (time.time() - start_time) * 1000

        record = self._audit_manager.create_audit_record(
            message_id=message_id,
            exchange="",
            routing_key=queue,
            producer_ip=None,
            consumer_ip=self._consumer_ip,
            body=body,
            ref_body=None,
            direction="rabbitmq_to_consumer",
            action=AuditAction.DROP,
            headers=headers,
            intercepted=True,
            interception_rule_id=rule_id,
            interception_reason=rule_name,
            latency_ms=latency_ms
        )

        self._audit_manager.record(record)
        self._otel.record_interception(message_id, rule_id, rule_name, "high")

        for callback in self._intercept_callbacks:
            try:
                callback(message_id, queue, body, headers, rule_id, rule_name)
            except Exception as e:
                logger.error(f"Intercept callback error: {e}")

        logger.warning(f"Consumed message {message_id} intercepted by rule {rule_id}: {rule_name}")

    def start(self) -> None:
        self._running = True
        self._ha_manager.start()
        logger.info(f"Message gateway {self.gateway_id} started")

    def stop(self) -> None:
        self._running = False
        self._ha_manager.stop()
        self._close_connection()
        self._audit_manager.flush()

        if self._virus_scan_engine:
            self._virus_scan_engine.stop()

        for sink in self._sinks:
            sink.close()

        logger.info(f"Message gateway {self.gateway_id} stopped")

    def register_publish_callback(self, callback: Callable) -> None:
        self._publish_callbacks.append(callback)

    def register_consume_callback(self, callback: Callable) -> None:
        self._consume_callbacks.append(callback)

    def register_intercept_callback(self, callback: Callable) -> None:
        self._intercept_callbacks.append(callback)

    def register_virus_alert_callback(self, callback: Callable) -> None:
        self._virus_alert_callbacks.append(callback)

    def get_virus_scan_status(self) -> Optional[Dict[str, Any]]:
        if self._virus_scan_engine:
            return self._virus_scan_engine.get_status()
        return None

    def get_quarantine_messages(self) -> List:
        if self._virus_scan_engine:
            return self._virus_scan_engine.get_quarantine_messages()
        return []

    def get_stats(self) -> Dict[str, Any]:
        ha_info = self._ha_manager.get_info()
        audit_summary = self._audit_manager.get_stats()
        perf_stats = self._perf_monitor.get_stats()

        return {
            "gateway_id": self.gateway_id,
            "ha": ha_info,
            "audit": audit_summary,
            "performance": perf_stats
        }

    def reset_stats(self) -> None:
        self._audit_manager.reset_stats()

    def get_interception_rules(self) -> List[Dict[str, Any]]:
        return self._interception_engine.get_rules_summary()

    def reload_interception_rules(self) -> None:
        self._interception_engine.reload_rules()
