"""全局配置与数据结构"""

from dataclasses import dataclass, field
from typing import List, Optional
import time


@dataclass
class ClientConfig:
    url: str
    rooms: int = 100
    clients: int = 5000
    duration: int = 300
    ramp_up: int = 120
    msg_min: int = 20
    msg_max: int = 200
    msg_interval: float = 1.0
    room_prefix: str = "room"
    redis_url: Optional[str] = None
    role: str = "single"
    node_id: str = ""
    output_dir: str = "./output"


@dataclass
class MessageRecord:
    client_id: str
    room: str
    send_ts: float
    recv_ts: float = 0.0
    latency: float = 0.0
    received: bool = False


@dataclass
class ConnectionRecord:
    client_id: str
    room: str
    connected: bool
    error: str = ""
    connect_time: float = 0.0


@dataclass
class TestReport:
    config: dict = field(default_factory=dict)
    connection_success_rate: float = 0.0
    total_connections: int = 0
    successful_connections: int = 0
    failed_connections: int = 0
    message_arrival_rate: float = 0.0
    total_sent: int = 0
    total_received: int = 0
    latency_p50: float = 0.0
    latency_p95: float = 0.0
    latency_p99: float = 0.0
    latency_avg: float = 0.0
    latency_min: float = 0.0
    latency_max: float = 0.0
    throughput_curve: List[dict] = field(default_factory=list)
    latency_distribution: List[dict] = field(default_factory=list)
    reconnect_attempts: int = 0
    reconnect_successes: int = 0
    reconnect_success_rate: float = 0.0
    reconnect_per_client_avg: float = 0.0
    node_id: str = ""
    start_time: float = 0.0
    end_time: float = 0.0
