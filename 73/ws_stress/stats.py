"""统计分析模块 - 延迟百分位、成功率、到达率、吞吐量统计"""

import math
import time
from collections import defaultdict
from threading import Lock
from typing import Dict, List, Tuple

from .config import TestReport


class StatisticsCollector:
    _instance = None
    _lock = Lock()

    def __new__(cls):
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
        return cls._instance

    def __init__(self):
        if hasattr(self, '_initialized'):
            return
        self._initialized = True
        self.reset()

    def reset(self):
        self._conn_records: List[Tuple[str, bool, float]] = []
        self._msg_latencies: List[float] = []
        self._sent_count: int = 0
        self._recv_count: int = 0
        self._throughput_samples: Dict[float, int] = defaultdict(int)
        self._latency_buckets: Dict[str, int] = defaultdict(int)
        self._errors: List[str] = []
        self._start_time: float = 0.0
        self._end_time: float = 0.0
        self._reconnect_attempts: int = 0
        self._reconnect_successes: int = 0

    def start(self):
        self._start_time = time.time()

    def stop(self):
        self._end_time = time.time()

    def record_connection(self, client_id: str, success: bool, connect_time: float = 0.0):
        self._conn_records.append((client_id, success, connect_time))

    def record_message_sent(self):
        self._sent_count += 1
        bucket = math.floor(time.time())
        self._throughput_samples[bucket] += 1

    def record_message_received(self, latency: float):
        self._recv_count += 1
        self._msg_latencies.append(latency)
        bucket_label = self._bucket_latency(latency)
        self._latency_buckets[bucket_label] += 1

    def record_error(self, error_msg: str):
        self._errors.append(error_msg)

    def record_reconnect_attempt(self, client_id: str):
        self._reconnect_attempts += 1

    def record_reconnect_success(self, client_id: str):
        self._reconnect_successes += 1

    @staticmethod
    def _bucket_latency(latency: float) -> str:
        if latency < 10:
            return "0-10ms"
        elif latency < 25:
            return "10-25ms"
        elif latency < 50:
            return "25-50ms"
        elif latency < 100:
            return "50-100ms"
        elif latency < 200:
            return "100-200ms"
        elif latency < 500:
            return "200-500ms"
        else:
            return "500ms+"

    @staticmethod
    def _percentile(sorted_data: List[float], p: float) -> float:
        if not sorted_data:
            return 0.0
        k = (len(sorted_data) - 1) * p
        f = math.floor(k)
        c = math.ceil(k)
        if f == c:
            return sorted_data[int(k)]
        d0 = sorted_data[int(f)] * (c - k)
        d1 = sorted_data[int(c)] * (k - f)
        return d0 + d1

    def build_report(self, config: dict, node_id: str = "") -> TestReport:
        sorted_latencies = sorted(self._msg_latencies)
        total_connections = len(self._conn_records)
        successful = sum(1 for _, s, _ in self._conn_records if s)
        failed = total_connections - successful

        conn_success_rate = (successful / total_connections * 100) if total_connections > 0 else 0.0
        msg_arrival_rate = (self._recv_count / self._sent_count * 100) if self._sent_count > 0 else 0.0

        throughput_curve = []
        if self._throughput_samples:
            min_ts = min(self._throughput_samples.keys())
            max_ts = max(self._throughput_samples.keys())
            elapsed = max_ts - min_ts + 1 if max_ts > min_ts else 1
            for i in range(int(elapsed)):
                ts = min_ts + i
                cnt = self._throughput_samples.get(ts, 0)
                throughput_curve.append({"time": i, "messages_per_sec": cnt})

        bucket_order = ["0-10ms", "10-25ms", "25-50ms", "50-100ms", "100-200ms", "200-500ms", "500ms+"]
        latency_dist = [{"range": b, "count": self._latency_buckets.get(b, 0)} for b in bucket_order]

        reconnect_success_rate = (
            round(self._reconnect_successes / self._reconnect_attempts * 100, 2)
            if self._reconnect_attempts > 0 else 0.0
        )
        reconnect_per_client_avg = (
            round(self._reconnect_attempts / successful, 2) if successful > 0 else 0.0
        )

        report = TestReport(
            config=config,
            connection_success_rate=round(conn_success_rate, 2),
            total_connections=total_connections,
            successful_connections=successful,
            failed_connections=failed,
            message_arrival_rate=round(msg_arrival_rate, 2),
            total_sent=self._sent_count,
            total_received=self._recv_count,
            latency_p50=round(self._percentile(sorted_latencies, 0.50), 2),
            latency_p95=round(self._percentile(sorted_latencies, 0.95), 2),
            latency_p99=round(self._percentile(sorted_latencies, 0.99), 2),
            latency_avg=round(sum(sorted_latencies) / len(sorted_latencies), 2) if sorted_latencies else 0.0,
            latency_min=round(sorted_latencies[0], 2) if sorted_latencies else 0.0,
            latency_max=round(sorted_latencies[-1], 2) if sorted_latencies else 0.0,
            throughput_curve=throughput_curve,
            latency_distribution=latency_dist,
            reconnect_attempts=self._reconnect_attempts,
            reconnect_successes=self._reconnect_successes,
            reconnect_success_rate=reconnect_success_rate,
            reconnect_per_client_avg=reconnect_per_client_avg,
            node_id=node_id,
            start_time=self._start_time,
            end_time=self._end_time,
        )
        return report
