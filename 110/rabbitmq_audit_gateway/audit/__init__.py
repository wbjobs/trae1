"""Audit data models and manager"""
import time
import uuid
import json
import threading
from dataclasses import dataclass, field, asdict
from typing import Optional, Dict, Any, List
from datetime import datetime
from enum import Enum


class RefCountedBody:
    """零拷贝引用计数的消息体封装，避免大消息复制"""
    
    __slots__ = ('_body', '_ref_count', '_lock', '_size')
    
    def __init__(self, body: bytes):
        self._body = body
        self._ref_count = 1
        self._lock = threading.Lock()
        self._size = len(body) if body else 0
        
    def retain(self) -> 'RefCountedBody':
        """增加引用计数"""
        with self._lock:
            self._ref_count += 1
        return self
        
    def release(self) -> None:
        """减少引用计数，当为0时释放内存"""
        with self._lock:
            self._ref_count -= 1
            if self._ref_count <= 0:
                self._body = b''
                self._size = 0
                
    @property
    def body(self) -> bytes:
        """获取消息体，不复制"""
        return self._body
        
    @property
    def size(self) -> int:
        """获取消息体大小"""
        return self._size
        
    def get_prefix(self, size: int) -> bytes:
        """获取消息体前缀，不复制原消息"""
        if not self._body:
            return b''
        return self._body[:size]
        
    def get_suffix(self, size: int) -> bytes:
        """获取消息体后缀，不复制原消息"""
        if not self._body:
            return b''
        return self._body[-size:] if len(self._body) > size else self._body
        
    def __del__(self):
        """析构时确保释放"""
        self.release()


class AuditAction(Enum):
    PUBLISH = "publish"
    CONSUME = "consume"
    INTERCEPT = "intercept"
    DROP = "drop"


class MessageDirection(Enum):
    PRODUCER_TO_RABBITMQ = "producer_to_rabbitmq"
    RABBITMQ_TO_CONSUMER = "rabbitmq_to_consumer"


@dataclass
class AuditRecord:
    message_id: str
    exchange: str
    routing_key: str
    timestamp: float
    producer_ip: Optional[str]
    consumer_ip: Optional[str]
    message_body_size: int
    direction: str
    action: str
    headers: Dict[str, Any] = field(default_factory=dict)
    body_preview: Optional[str] = None
    intercepted: bool = False
    interception_rule_id: Optional[str] = None
    interception_reason: Optional[str] = None
    sampled: bool = False
    latency_ms: Optional[float] = None
    gateway_instance_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    record_time: float = field(default_factory=time.time)
    # 新增字段：大消息处理
    is_large_message: bool = False
    content_skipped: bool = False  # 是否跳过了内容审计（只审计元数据）

    def to_dict(self) -> Dict[str, Any]:
        return asdict(self)

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), default=str)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'AuditRecord':
        return cls(**data)

    def should_store_body(self, max_preview_size: int = 1024) -> bool:
        if self.content_skipped:
            return False
        if self.body_preview is not None:
            return True
        return False


@dataclass
class AuditBatch:
    records: List[AuditRecord] = field(default_factory=list)
    batch_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    created_at: float = field(default_factory=time.time)

    def add(self, record: AuditRecord) -> None:
        self.records.append(record)

    def size(self) -> int:
        return len(self.records)

    def clear(self) -> None:
        self.records.clear()

    def to_dict(self) -> Dict[str, Any]:
        return {
            "batch_id": self.batch_id,
            "created_at": self.created_at,
            "record_count": len(self.records),
            "records": [r.to_dict() for r in self.records]
        }


class AuditStats:
    def __init__(self):
        self._lock = None
        self.total_messages = 0
        self.total_published = 0
        self.total_consumed = 0
        self.total_intercepted = 0
        self.total_dropped = 0
        self.total_sampled = 0
        self.total_bytes = 0
        # 新增大消息统计
        self.total_large_messages = 0
        self.total_content_skipped = 0
        self.messages_by_exchange: Dict[str, int] = {}
        self.messages_by_routing_key: Dict[str, int] = {}
        self.interceptions_by_rule: Dict[str, int] = {}
        self.start_time = time.time()
        self._last_reset = time.time()

    def increment(self, action: AuditAction, count: int = 1) -> None:
        self.total_messages += count
        if action == AuditAction.PUBLISH:
            self.total_published += count
        elif action == AuditAction.CONSUME:
            self.total_consumed += count
        elif action == AuditAction.INTERCEPT:
            self.total_intercepted += count
        elif action == AuditAction.DROP:
            self.total_dropped += count

    def add_bytes(self, count: int) -> None:
        self.total_bytes += count

    def track_exchange(self, exchange: str) -> None:
        self.messages_by_exchange[exchange] = self.messages_by_exchange.get(exchange, 0) + 1

    def track_routing_key(self, routing_key: str) -> None:
        self.messages_by_routing_key[routing_key] = self.messages_by_routing_key.get(routing_key, 0) + 1

    def track_interception(self, rule_id: str) -> None:
        self.interceptions_by_rule[rule_id] = self.interceptions_by_rule.get(rule_id, 0) + 1

    def track_sampled(self) -> None:
        self.total_sampled += 1

    def track_large_message(self) -> None:
        self.total_large_messages += 1

    def track_content_skipped(self) -> None:
        self.total_content_skipped += 1

    def get_summary(self) -> Dict[str, Any]:
        uptime = time.time() - self.start_time
        return {
            "uptime_seconds": uptime,
            "total_messages": self.total_messages,
            "total_published": self.total_published,
            "total_consumed": self.total_consumed,
            "total_intercepted": self.total_intercepted,
            "total_dropped": self.total_dropped,
            "total_sampled": self.total_sampled,
            "total_bytes": self.total_bytes,
            # 新增统计
            "total_large_messages": self.total_large_messages,
            "total_content_skipped": self.total_content_skipped,
            "messages_per_second": self.total_messages / uptime if uptime > 0 else 0,
            "messages_by_exchange": dict(self.messages_by_exchange),
            "messages_by_routing_key": dict(self.messages_by_routing_key),
            "interceptions_by_rule": dict(self.interceptions_by_rule),
            "last_reset": self._last_reset
        }

    def reset(self) -> None:
        self.total_messages = 0
        self.total_published = 0
        self.total_consumed = 0
        self.total_intercepted = 0
        self.total_dropped = 0
        self.total_sampled = 0
        self.total_bytes = 0
        self.total_large_messages = 0
        self.total_content_skipped = 0
        self.messages_by_exchange.clear()
        self.messages_by_routing_key.clear()
        self.interceptions_by_rule.clear()
        self._last_reset = time.time()


class AuditManager:
    def __init__(self, config, sinks, stats: Optional[AuditStats] = None):
        self.config = config
        self.sinks = sinks
        self.stats = stats or AuditStats()
        self._batch = AuditBatch()
        self._enabled = config.audit.enabled
        self._batch_size = config.audit.batch_size
        self._flush_interval = config.audit.flush_interval
        self._last_flush = time.time()
        # 新增大消息配置
        self._max_body_size = config.audit.max_body_size
        self._preview_size = config.audit.preview_size

    def record(self, record: AuditRecord) -> None:
        if not self._enabled:
            return

        self.stats.increment(AuditAction(record.action))
        self.stats.add_bytes(record.message_body_size)
        self.stats.track_exchange(record.exchange)
        self.stats.track_routing_key(record.routing_key)

        # 跟踪大消息
        if record.is_large_message:
            self.stats.track_large_message()

        # 跟踪跳过内容审计
        if record.content_skipped:
            self.stats.track_content_skipped()

        if record.intercepted and record.interception_rule_id:
            self.stats.track_interception(record.interception_rule_id)

        if record.sampled:
            self.stats.track_sampled()

        if record.should_store_body():
            self._batch.add(record)

        if self._batch.size() >= self._batch_size or \
           (time.time() - self._last_flush) >= self._flush_interval:
            self.flush()

    @property
    def max_body_size(self) -> int:
        return self._max_body_size

    @property
    def preview_size(self) -> int:
        return self._preview_size

    def is_large_message(self, body_size: int) -> bool:
        return body_size > self._max_body_size

    def create_audit_record(
        self,
        message_id: str,
        exchange: str,
        routing_key: str,
        producer_ip: Optional[str],
        consumer_ip: Optional[str],
        body: bytes,
        ref_body: Optional[RefCountedBody],
        direction: str,
        action: AuditAction,
        headers: Dict[str, Any],
        sampled: bool = False,
        intercepted: bool = False,
        interception_rule_id: Optional[str] = None,
        interception_reason: Optional[str] = None,
        latency_ms: Optional[float] = None
    ) -> AuditRecord:
        body_size = len(body) if body else 0
        is_large = self.is_large_message(body_size)
        content_skipped = is_large

        body_preview = None
        if not content_skipped and body:
            preview_size = min(self._preview_size, body_size)
            body_preview = body[:preview_size].decode('utf-8', errors='ignore')

        record = AuditRecord(
            message_id=message_id,
            exchange=exchange,
            routing_key=routing_key,
            timestamp=time.time(),
            producer_ip=producer_ip,
            consumer_ip=consumer_ip,
            message_body_size=body_size,
            direction=direction,
            action=action.value,
            headers=headers,
            body_preview=body_preview,
            intercepted=intercepted,
            interception_rule_id=interception_rule_id,
            interception_reason=interception_reason,
            sampled=sampled,
            latency_ms=latency_ms,
            is_large_message=is_large,
            content_skipped=content_skipped
        )

        return record

    def flush(self) -> None:
        if self._batch.size() == 0:
            return

        for sink in self.sinks:
            try:
                sink.send_batch(self._batch)
            except Exception as e:
                pass

        self._batch.clear()
        self._last_flush = time.time()

    def get_stats(self) -> Dict[str, Any]:
        return self.stats.get_summary()

    def reset_stats(self) -> None:
        self.stats.reset()
