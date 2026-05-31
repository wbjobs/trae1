"""Data collector - aggregates events from the eBPF tracer.

Groups stack samples and timing info by query, producing structured
records suitable for flame graph generation and reporting.
"""

import logging
import time
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from .tracer import TracerEventType

logger = logging.getLogger(__name__)


@dataclass
class QueryEvent:
    """Represents a single slow query with all captured data."""

    query_id: str
    sql: str
    start_ts: float
    end_ts: float
    duration_ms: float
    io_wait_ms: float
    lock_wait_ms: float
    pid: int
    tid: int
    stack_samples: List[Dict] = field(default_factory=list)
    func_counts: Dict[str, int] = field(default_factory=lambda: defaultdict(int))
    func_times: Dict[str, float] = field(default_factory=lambda: defaultdict(float))
    sample_count: int = 0
    ai_suggestion: Optional[Dict] = None

    @property
    def cpu_time_ms(self) -> float:
        return max(0.0, self.duration_ms - self.io_wait_ms - self.lock_wait_ms)

    def to_dict(self) -> Dict:
        return {
            "query_id": self.query_id,
            "sql": self.sql,
            "start_ts": self.start_ts,
            "end_ts": self.end_ts,
            "duration_ms": round(self.duration_ms, 2),
            "io_wait_ms": round(self.io_wait_ms, 2),
            "lock_wait_ms": round(self.lock_wait_ms, 2),
            "cpu_time_ms": round(self.cpu_time_ms, 2),
            "pid": self.pid,
            "tid": self.tid,
            "sample_count": self.sample_count,
            "stack_samples": self.stack_samples,
            "func_counts": dict(self.func_counts),
            "func_times": {k: round(v, 2) for k, v in self.func_times.items()},
            "ai_suggestion": self.ai_suggestion,
        }


class DataCollector:
    """Collects and aggregates eBPF events into query-level records."""

    def __init__(self, max_events: int = 10000):
        self.max_events = max_events
        self._pending_queries: Dict[str, QueryEvent] = {}
        self._completed_queries: List[QueryEvent] = []
        self._sample_buffer: Dict[str, List[Dict]] = defaultdict(list)
        self._event_count = 0

    def _make_query_id(self, tid: int, ts: float) -> str:
        return f"q_{tid}_{int(ts * 1_000_000)}"

    def handle_event(self, event) -> Optional[QueryEvent]:
        """Process a single eBPF event. Returns a QueryEvent if a query completes."""
        self._event_count += 1
        event_type = event.type
        tid = event.tid
        timestamp = event.timestamp / 1_000_000_000.0

        if event_type == TracerEventType.EVENT_QUERY_END:
            qid = self._make_query_id(tid, timestamp)
            qe = QueryEvent(
                query_id=qid,
                sql=event.query.decode("utf-8", errors="replace") if isinstance(event.query, bytes) else str(event.query),
                start_ts=timestamp - (event.duration_ns / 1_000_000_000.0),
                end_ts=timestamp,
                duration_ms=event.duration_ns / 1_000_000.0,
                io_wait_ms=event.io_wait_ns / 1_000_000.0,
                lock_wait_ms=event.lock_wait_ns / 1_000_000.0,
                pid=event.pid,
                tid=tid,
                sample_count=0,
            )

            tid_samples = self._sample_buffer.pop(str(tid), [])
            qe.stack_samples = tid_samples
            qe.sample_count = len(tid_samples)

            self._completed_queries.append(qe)

            if len(self._completed_queries) > self.max_events:
                self._completed_queries = self._completed_queries[-self.max_events:]

            logger.debug(
                f"Slow query: {qe.sql[:80]}... "
                f"dur={qe.duration_ms:.1f}ms io={qe.io_wait_ms:.1f}ms "
                f"lock={qe.lock_wait_ms:.1f}ms samples={qe.sample_count}"
            )
            return qe

        elif event_type == TracerEventType.EVENT_STACK_SAMPLE:
            func_name = (
                event.func_name.decode("utf-8", errors="replace")
                if isinstance(event.func_name, bytes)
                else str(event.func_name)
            )
            sample = {
                "timestamp": timestamp,
                "stack_id": event.stack_id,
                "func_name": func_name,
                "tid": tid,
            }
            self._sample_buffer[str(tid)].append(sample)

        return None

    def get_completed_queries(self) -> List[QueryEvent]:
        return list(self._completed_queries)

    def get_summary(self) -> Dict:
        queries = self._completed_queries
        if not queries:
            return {"total_queries": 0}

        durations = [q.duration_ms for q in queries]
        io_waits = [q.io_wait_ms for q in queries]
        lock_waits = [q.lock_wait_ms for q in queries]

        return {
            "total_queries": len(queries),
            "total_events": self._event_count,
            "avg_duration_ms": round(sum(durations) / len(durations), 2),
            "max_duration_ms": round(max(durations), 2),
            "min_duration_ms": round(min(durations), 2),
            "avg_io_wait_ms": round(sum(io_waits) / len(io_waits), 2),
            "avg_lock_wait_ms": round(sum(lock_waits) / len(lock_waits), 2),
            "total_samples": sum(q.sample_count for q in queries),
            "p95_duration_ms": round(self._percentile(durations, 95), 2),
            "p99_duration_ms": round(self._percentile(durations, 99), 2),
        }

    @staticmethod
    def _percentile(data: List[float], p: int) -> float:
        if not data:
            return 0.0
        sorted_data = sorted(data)
        idx = int(len(sorted_data) * p / 100)
        idx = min(idx, len(sorted_data) - 1)
        return sorted_data[idx]

    def reset(self):
        self._pending_queries.clear()
        self._completed_queries.clear()
        self._sample_buffer.clear()
        self._event_count = 0
